#ifndef INDEXINSTANCE_H
#define INDEXINSTANCE_H

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <tuple>
#include "MemoryMappedFile.h"

class IndexInstance {
public:
    // Construct from file path; optional archiveIndex for group archives
    IndexInstance(const std::string& path, int16_t archiveIndex = -1);
    ~IndexInstance() = default;

    // Returns (offset, size, archiveIndex). If not found: (-1, -1, -1)
    std::tuple<int32_t, int32_t, int16_t>
    GetIndexInfo(std::span<const uint8_t> eKeyTarget) const;

    struct Entry {
        std::vector<uint8_t> eKey;
        int offset;
        int size;
        int archiveIndex;
    };

    std::vector<Entry> GetAllEntries();

private:
    struct IndexFooter {
        uint8_t formatRevision, flags0, flags1;
        uint8_t blockSizeKBytes, offsetBytes, sizeBytes, keyBytes, hashBytes;
        uint32_t numElements;
        uint8_t footerHash[8];
    };

    std::shared_ptr<MemoryMappedFile>   mmf_;
    uint8_t const*      fileData_;
    size_t              indexSize_;

    IndexFooter         footer_;
    bool                isFileIndex_;
    bool                isGroupArchive_;
    int16_t             archiveIndex_;

    size_t  blockSizeBytes_, entrySize_;
    int     entriesPerBlock_, entriesInLastBlock_, numBlocks_;
    size_t  ofsStartOfToc_, ofsEndOfTocEkeys_;
};

#endif //INDEXINSTANCE_H
