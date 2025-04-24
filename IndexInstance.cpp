#include "IndexInstance.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>

#include "utils/BinaryUtils.h"

IndexInstance::IndexInstance(const std::string& path, int16_t archiveIndex)
    : fileData_(nullptr), archiveIndex_(archiveIndex)
{
    mmf_ = std::make_shared<MemoryMappedFile>(path, false);

    if (!mmf_->isOpen()) {
        throw std::runtime_error("Failed to open memory-mapped file: " + path);
    }
    fileData_ = static_cast<uint8_t const*>(mmf_->data());
    indexSize_ = mmf_->size();

    if (indexSize_ < sizeof(IndexFooter)) {
        throw std::runtime_error("File too small to contain IndexFooter");
    }

    // Read footer from end-of-file
    footer_ = *reinterpret_cast<IndexFooter const*>(fileData_ + indexSize_ - sizeof(IndexFooter));

    isFileIndex_    = (footer_.offsetBytes == 0);
    isGroupArchive_ = (footer_.offsetBytes == 6);

    blockSizeBytes_     = static_cast<size_t>(footer_.blockSizeKBytes) << 10;

    entrySize_          = footer_.keyBytes + footer_.sizeBytes + footer_.offsetBytes;
    entriesPerBlock_    = static_cast<int>(blockSizeBytes_ / entrySize_);

    numBlocks_          = static_cast<int>(std::ceil(static_cast<double>(numElements_) / entriesPerBlock_));
    entriesInLastBlock_ = static_cast<int>(numElements_) - (numBlocks_ - 1) * entriesPerBlock_;

    ofsStartOfToc_    = static_cast<size_t>(numBlocks_) * blockSizeBytes_;
    ofsEndOfTocEkeys_ = ofsStartOfToc_ + static_cast<size_t>(footer_.keyBytes) * numBlocks_;
}

std::tuple<int32_t, int32_t, int16_t>
IndexInstance::GetIndexInfo(std::span<const uint8_t> eKeyTarget) const
{
    // 1) Block-level binary search on TOC e-keys
    auto tocStart = fileData_ + ofsStartOfToc_;
    auto tocEnd   = fileData_ + ofsEndOfTocEkeys_;

    EntryIterator beginIt(tocStart, footer_.keyBytes);
    EntryIterator endIt(tocEnd, footer_.keyBytes);

    auto blockIt = std::lower_bound(beginIt, endIt, eKeyTarget.data(),
        [&](uint8_t const* lhs, uint8_t const* rhs){
            return std::memcmp(lhs, rhs, footer_.keyBytes) < 0;
        }
    );

    if (blockIt == endIt)
        return {-1, -1, -1};

    int blockIndex = static_cast<int>(std::distance(beginIt, blockIt));

    // 2) Intra-block search among full entries
    auto blockBase = fileData_ + static_cast<size_t>(blockIndex) * blockSizeBytes_;
    int nEntries = (blockIndex < numBlocks_ - 1 ? entriesPerBlock_ : entriesInLastBlock_);

    std::vector<uint8_t const*> entryPtrs;
    entryPtrs.reserve(nEntries);
    for (int i = 0; i < nEntries; ++i) {
        entryPtrs.push_back(blockBase + static_cast<size_t>(i) * entrySize_);
    }

    auto entryIt = std::lower_bound(
        entryPtrs.begin(), entryPtrs.end(),
        eKeyTarget.data(),
        [&](uint8_t const* lhs, uint8_t const* rhs){
            return std::memcmp(lhs, rhs, footer_.keyBytes) < 0;
        }
    );

    if (entryIt == entryPtrs.end())
        return {-1, -1, -1};

    auto entry = *entryIt;
    if (std::memcmp(entry, eKeyTarget.data(), footer_.keyBytes) != 0)
        return {-1, -1, -1};

    // Parse offset/size/archiveIndex
    int32_t size = 0, offset = -1;
    int16_t arcIdx = archiveIndex_;
    auto ptr = entry + footer_.keyBytes;

    // size always big-endian 32-bit
    size = ReadInt32BE(ptr);
    ptr += footer_.sizeBytes;

    if (isGroupArchive_) {
        arcIdx = static_cast<int16_t>(ReadUInt16BE(ptr));
        ptr += 2;
        offset = ReadInt32BE(ptr);
    } else if (!isFileIndex_) {
        // offsetBytes may be <=4; for <=2 use ReadUInt16BE, else ReadInt32BE
        if (footer_.offsetBytes == 2)
            offset = static_cast<int32_t>(ReadUInt16BE(ptr));
        else
            offset = ReadInt32BE(ptr);
    }

    return {offset, size, arcIdx};
}
