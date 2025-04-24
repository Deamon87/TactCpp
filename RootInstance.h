#ifndef ROOTINSTANCE_H
#define ROOTINSTANCE_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

class RootInstance {
public:
    enum class LoadMode : uint32_t {
        Normal,
        Full
    };

    enum class LocaleFlags : uint32_t {
        All       = 0xFFFFFFFF,
        None      = 0,
        Unk_1     = 0x1,
        enUS      = 0x2,
        koKR      = 0x4,
        Unk_8     = 0x8,
        frFR      = 0x10,
        deDE      = 0x20,
        zhCN      = 0x40,
        esES      = 0x80,
        zhTW      = 0x100,
        enGB      = 0x200,
        enCN      = 0x400,
        enTW      = 0x800,
        esMX      = 0x1000,
        ruRU      = 0x2000,
        ptBR      = 0x4000,
        itIT      = 0x8000,
        ptPT      = 0x10000,
        enSG      = 0x20000000,
        plPL      = 0x40000000,
        All_WoW   = enUS | koKR | frFR | deDE | zhCN | esES | zhTW | enGB | esMX | ruRU | ptBR | itIT | ptPT
    };

    enum class ContentFlags : uint32_t {
        None             = 0,
        F00000001        = 0x1,
        F00000002        = 0x2,
        F00000004        = 0x4,
        LoadOnWindows    = 0x8,
        LoadOnMacOS      = 0x10,
        LowViolence      = 0x80,
        DoNotLoad        = 0x100,
        UpdatePlugin     = 0x800,
        Encrypted        = 0x8000000,
        NoNames          = 0x10000000,
        UncommonRes      = 0x20000000,
        Bundle           = 0x40000000,
        NoCompression    = 0x80000000
    };

    struct MD5 {
        static constexpr size_t Length = 16;
        uint8_t data[Length];
        MD5() = default;
        explicit MD5(const uint8_t* src);
    };

    struct RootEntry {
        ContentFlags contentFlags;
        LocaleFlags  localeFlags;
        uint64_t     lookup;
        uint32_t     fileDataID;
        MD5          md5;
    };

    struct Settings {
        LoadMode    rootMode;
        LocaleFlags locale;
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
    LoadMode                                m_loadedWith;
    std::unordered_map<uint64_t, uint32_t>  entriesLookup;
    std::unordered_map<uint32_t, RootEntry> entriesFDID;
    std::unordered_map<uint32_t, std::vector<RootEntry>> entriesFDIDFull;

    static uint32_t ReadUInt32LE(const std::vector<uint8_t>& data, size_t offset);
    static uint64_t ReadUInt64LE(const std::vector<uint8_t>& data, size_t offset);
};

#endif