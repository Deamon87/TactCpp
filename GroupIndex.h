//
// Created by Deamon on 4/23/2025.
//

#ifndef GROUPINDEX_H
#define GROUPINDEX_H
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "CDN.h"

class GroupIndex {
    struct IndexEntry {
        std::vector<uint8_t> EKey;   // 16 bytes
        uint32_t Size;
        uint16_t ArchiveIndex;
        uint32_t Offset;
    };

    std::vector<IndexEntry> Entries;
    std::mutex entryMutex;

public:
    /// Generates (or validates) the merged group .index file.
    /// @param hash   expected filename-hash, or empty to auto-compute
    /// @return the final index filename (MD5 footer)
    std::string Generate(
        const std::shared_ptr<CDN> &cdn,
        const Settings &settings,
        const std::string& hash,
        const std::vector<std::string>& archives);
};


#endif //GROUPINDEX_H
