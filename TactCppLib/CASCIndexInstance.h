#include <windows.h>
#include <string>
#include <stdexcept>
#include <tuple>
#include <span>
#include <cstdint>
#include <algorithm>
#include <cstring>

#include "MemoryMappedFile.h"
#include "utils/BinaryUtils.h"

#pragma pack(push, 1)
struct IndexHeader {
    uint32_t headerHashSize;
    uint32_t headerHash;
    uint16_t version;
    uint8_t  bucketIndex;
    uint8_t  extraBytes;
    uint8_t  entrySizeBytes;
    uint8_t  entryOffsetBytes;
    uint8_t  entryKeyBytes;
    uint8_t  entryOffsetBits;
    uint64_t maxArchiveSize;
    uint8_t  padding[8];
    uint32_t entriesSize;
    uint32_t entriesHash;
};
#pragma pack(pop)

static_assert(sizeof(IndexHeader) == 40, "IndexHeader must be exactly 40 bytes");

class CASCIndexInstance {
private:
    uint8_t* fileData = nullptr;
    std::shared_ptr<MemoryMappedFile> file;

    size_t indexSize = 0;

    IndexHeader header;
    size_t entrySize = 0;
    size_t ofsStartOfEntries = 0;
    size_t ofsEndOfEntries = 0;

public:
    explicit CASCIndexInstance(const std::string& path) {
        // Open file
        file = std::make_shared<MemoryMappedFile>(path);
        fileData = static_cast<uint8_t *>(file->data());

        indexSize = file->size();

        // Read header
        header = *reinterpret_cast<IndexHeader*>(file->data());

        // Compute entry layout
        entrySize = static_cast<size_t>(header.entrySizeBytes +
                                        header.entryOffsetBytes +
                                        header.entryKeyBytes);
        ofsStartOfEntries = sizeof(IndexHeader);
        ofsEndOfEntries = ofsStartOfEntries + header.entriesSize;
    }

    ~CASCIndexInstance() {
    }

    struct FileArchiveData {int archiveOffset; int archiveSize; int archiveIndex;};

    // Returns tuple(offset, size, archiveIndex), or (-1,-1,-1) if not found
     FileArchiveData GetIndexInfo(std::span<const uint8_t> eKeyTarget) {
        if (eKeyTarget.size() < header.entryKeyBytes)
            return {-1, -1, -1};

        const uint8_t* key = eKeyTarget.data();
        // Define iterators over entries
        EntryIterator beginIt(fileData + ofsStartOfEntries, entrySize);
        EntryIterator endIt(fileData + ofsEndOfEntries, entrySize);

        // Comparator for lower_bound
        auto cmp = [this](const uint8_t* lhs, const uint8_t* key) {
            return std::memcmp(lhs, key, header.entryKeyBytes) < 0;
        };

        // Use std::lower_bound to find the first entry >= key
        auto it = std::lower_bound(beginIt, endIt, key, cmp);
        const uint8_t* found = (it != endIt) ? *it : (fileData + ofsStartOfEntries);

        if (found == fileData + ofsStartOfEntries)
            return {-1, -1, -1};

        size_t idx = (found - (fileData + ofsStartOfEntries)) / entrySize;
        uint8_t* entryPtr = fileData + ofsStartOfEntries + idx * entrySize;

        // Verify exact match of key prefix
        if (std::memcmp(entryPtr, key, header.entryKeyBytes) != 0)
            return {-1, -1, -1};

        // Read archiveIndex and offsets
        uint8_t indexHigh = *(entryPtr + header.entryKeyBytes);
        const uint8_t* pLow = entryPtr + header.entryKeyBytes + 1;
        uint32_t indexLow = ReadInt32BE(pLow);
        const uint8_t* pSize = entryPtr + header.entryKeyBytes + 5;
        uint32_t rawSize = ReadInt32LE(pSize);

        int dataSize = int(rawSize) - 30;

        int archiveIdx = (int(indexHigh) << 2) |
                         int((indexLow & 0xC0000000) >> 30);
        int archiveOff = int(indexLow & 0x3FFFFFFF) + 30;

        return {archiveOff, dataSize, archiveIdx};
    }
};
