//
// Created by Deamon on 4/23/2025.
//

#ifndef INSTALLINSTANCE_H
#define INSTALLINSTANCE_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
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
    ~InstallInstance();

    const std::vector<InstallTagEntry>&   getTags()    const;
    const std::vector<InstallFileEntry>&  getEntries() const;
    const bool getCKeyByName(const std::string& fileName, std::vector<uint8_t>& cKeyTarget) const;

private:
    std::shared_ptr<MemoryMappedFile>       mmf_;
    uint8_t                                 HashSize_;
    uint16_t                                NumTags_;
    uint32_t                                NumEntries_;
    std::vector<InstallTagEntry>            Tags_;
    std::vector<InstallFileEntry>           Entries_;
    std::unordered_map<std::string, uint32_t>           EntriesByName;
};

#endif //INSTALLINSTANCE_H
