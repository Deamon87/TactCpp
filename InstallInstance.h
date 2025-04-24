//
// Created by Deamon on 4/23/2025.
//

#ifndef INSTALLINSTANCE_H
#define INSTALLINSTANCE_H

#include <string>
#include <vector>
#include <cstdint>
#include "MemoryMappedFile.h"

struct InstallTagEntry {
    std::string         name;
    uint16_t            type;
    std::vector<bool>   files;
};

struct InstallFileEntry {
    std::string               name;    // normalized to '\\'
    std::vector<uint8_t>      md5;     // content hash
    uint32_t                  size;
    std::vector<std::string>  tags;
};

class InstallInstance {
public:
    explicit InstallInstance(const std::string& path);

    const std::vector<InstallTagEntry>&   getTags()    const;
    const std::vector<InstallFileEntry>&  getEntries() const;

private:
    MemoryMappedFile                        mmf_;
    uint8_t                                 HashSize_;
    uint16_t                                NumTags_;
    uint32_t                                NumEntries_;
    std::vector<InstallTagEntry>            Tags_;
    std::vector<InstallFileEntry>           Entries_;
};

#endif //INSTALLINSTANCE_H
