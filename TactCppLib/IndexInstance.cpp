#include "IndexInstance.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <stdexcept>

#include "utils/BinaryUtils.h"
#include "utils/DataReader.h"

IndexInstance::IndexInstance(const std::string &path, int16_t archiveIndex)
    : fileData_(nullptr), archiveIndex_(archiveIndex) {
    mmf_ = std::make_shared<MemoryMappedFile>(path, false);

    if (!mmf_->isOpen()) {
        throw std::runtime_error("Failed to open memory-mapped file: " + path);
    }
    fileData_ = static_cast<uint8_t const *>(mmf_->data());
    indexSize_ = mmf_->size();

    if (indexSize_ < sizeof(IndexFooter)) {
        throw std::runtime_error("File too small to contain IndexFooter");
    }

    // Read footer from end-of-file
    footer_ = *reinterpret_cast<IndexFooter const *>(fileData_ + indexSize_ - sizeof(IndexFooter));

    isFileIndex_ = (footer_.offsetBytes == 0);
    isGroupArchive_ = (footer_.offsetBytes == 6);

    blockSizeBytes_ = static_cast<size_t>(footer_.blockSizeKBytes) << 10;

    entrySize_ = footer_.keyBytes + footer_.sizeBytes + footer_.offsetBytes;
    entriesPerBlock_ = static_cast<int>(blockSizeBytes_ / entrySize_);

    numBlocks_ = static_cast<int>(std::ceil(static_cast<double>(footer_.numElements) / entriesPerBlock_));
    entriesInLastBlock_ = static_cast<int>(footer_.numElements) - (numBlocks_ - 1) * entriesPerBlock_;

    ofsStartOfToc_ = static_cast<size_t>(numBlocks_) * blockSizeBytes_;
    ofsEndOfTocEkeys_ = ofsStartOfToc_ + static_cast<size_t>(footer_.keyBytes) * numBlocks_;
}

std::tuple<int32_t, int32_t, int16_t>
IndexInstance::GetIndexInfo(std::span<const uint8_t> eKeyTarget) const {
    // 1) Block-level binary search on TOC e-keys
    auto tocStart = fileData_ + ofsStartOfToc_;
    auto tocEnd = fileData_ + ofsEndOfTocEkeys_;

    EntryIterator beginIt(tocStart, footer_.keyBytes);
    EntryIterator endIt(tocEnd, footer_.keyBytes);

    auto blockIt = std::lower_bound(beginIt, endIt, eKeyTarget.data(),
                                    [&](uint8_t const *lhs, uint8_t const *rhs) {
                                        return std::memcmp(lhs, rhs, footer_.keyBytes) < 0;
                                    }
    );

    if (blockIt == endIt)
        return {-1, -1, -1};

    int blockIndex = static_cast<int>(std::distance(beginIt, blockIt));

    // 2) Intra-block search among full entries
    auto blockBase = fileData_ + static_cast<size_t>(blockIndex) * blockSizeBytes_;
    int nEntries = (blockIndex < numBlocks_ - 1 ? entriesPerBlock_ : entriesInLastBlock_);

    std::vector<uint8_t const *> entryPtrs;
    entryPtrs.reserve(nEntries);
    for (int i = 0; i < nEntries; ++i) {
        entryPtrs.push_back(blockBase + static_cast<size_t>(i) * entrySize_);
    }

    auto entryIt = std::lower_bound(
        entryPtrs.begin(), entryPtrs.end(),
        eKeyTarget.data(),
        [&](uint8_t const *lhs, uint8_t const *rhs) {
            return std::memcmp(lhs, rhs, footer_.keyBytes) < 0;
        }
    );

    if (entryIt == entryPtrs.end())
        return {-1, -1, -1};

    auto entry = *entryIt;
    if (std::memcmp(entry, eKeyTarget.data(), footer_.keyBytes) != 0)
        return {-1, -1, -1};

    DataReader dr(const_cast<uint8_t*>(entry), entrySize_);
    // skip over the key
    dr.SetOffset(footer_.keyBytes);

    // size always big-endian 32-bit
    assert(footer_.sizeBytes == 4);
    int32_t size = dr.ReadInt32BE();

    int32_t offset = -1;
    int16_t arcIdx = archiveIndex_;

    if (isGroupArchive_) {
        arcIdx = dr.ReadUInt16BE();
        offset = dr.ReadInt32BE();
    }
    else if (!isFileIndex_) {
        // only non-file-index archives have an offset
        if (footer_.offsetBytes == 2) {
            offset = dr.ReadUInt16BE();
        } else {
            offset = dr.ReadInt32BE();
        }
    }

    return {offset, size, arcIdx};
}

std::vector<IndexInstance::Entry> IndexInstance::GetAllEntries() {
    std::vector<Entry> entries;
    const uint8_t *fileData = fileData_;

    for (int i = 0; i < numBlocks_; ++i) {
        const uint8_t *startOfBlock = fileData + i * blockSizeBytes_;

        for (int j = 0; j < entriesPerBlock_; ++j) {
            const uint8_t *entryPtr = startOfBlock + j * entrySize_;

            DataReader dr(const_cast<uint8_t*>(entryPtr), entrySize_);

            std::vector<uint8_t> eKey = dr.ReadUint8Array(footer_.keyBytes);

            int32_t size   = 0;
            int32_t offset = -1;
            int     aIndex = archiveIndex_;

            if (isGroupArchive_) {
                size   = dr.ReadInt32BE();
                aIndex = dr.ReadInt16BE();
                offset = dr.ReadInt32BE();
            }
            else if (isFileIndex_) {
                size = dr.ReadInt32BE();
            }
            else {
                size   = dr.ReadInt32BE();
                offset = dr.ReadInt32BE();
            }

            if (size != 0) {
                entries.push_back({ std::move(eKey), offset, size, aIndex });
            }
        }
    }

    return entries;
}
