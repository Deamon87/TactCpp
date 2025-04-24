#ifndef ROOTINSTANCE_H
#define ROOTINSTANCE_H

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>

#include "Settings.h"
#include "wow/WoWRootFlags.h"

class RootInstance {
public:
    typedef std::array<uint8_t, 16> MD5;

    struct RootEntry {
        RootWoW::ContentFlags contentFlags;
        RootWoW::LocaleFlags  localeFlags;
        uint64_t     lookup;
        uint32_t     fileDataID;
        MD5          md5;
    };

    explicit RootInstance(const std::string& path, const Settings& settings);

    std::vector<RootEntry>    GetEntriesByFDID(uint32_t fileDataID) const;
    std::vector<RootEntry>    GetEntriesByLookup(uint64_t lookup) const;
    std::vector<uint32_t>     GetAvailableFDIDs() const;
    std::vector<uint64_t>     GetAvailableLookups() const;
    bool                      FileExists(uint64_t lookup) const;
    bool                      FileExists(uint32_t fileDataID) const;

private:
    std::vector<uint8_t>                    m_data;
    RootWoW::LoadMode                       m_loadedWith;
    std::unordered_map<uint64_t, uint32_t>  entriesLookup;
    std::unordered_map<uint32_t, RootEntry> entriesFDID;
    std::unordered_map<uint32_t, std::vector<RootEntry>> entriesFDIDFull;

    static uint32_t ReadUInt32LE(const std::vector<uint8_t>& data, size_t offset);
    static uint64_t ReadUInt64LE(const std::vector<uint8_t>& data, size_t offset);
};

#endif