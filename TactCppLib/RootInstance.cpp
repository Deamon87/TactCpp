#include "RootInstance.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <iostream>

#include "utils/DataReader.h"

using namespace std;

const std::unordered_map<std::string, RootWoW::LocaleFlags> RootInstance::StringToLocaleFlag = {
    {"dede", RootWoW::LocaleFlags::deDE},
    {"enus", RootWoW::LocaleFlags::enUS},
    {"engb", RootWoW::LocaleFlags::enGB},
    {"ruru", RootWoW::LocaleFlags::ruRU},
    {"zhcn", RootWoW::LocaleFlags::zhCN},
    {"zhtw", RootWoW::LocaleFlags::zhTW},
    {"entw", RootWoW::LocaleFlags::enTW},
    {"eses", RootWoW::LocaleFlags::esES},
    {"esmx", RootWoW::LocaleFlags::esMX},
    {"frfr", RootWoW::LocaleFlags::frFR},
    {"itit", RootWoW::LocaleFlags::itIT},
    {"kokr", RootWoW::LocaleFlags::koKR},
    {"ptbr", RootWoW::LocaleFlags::ptBR},
    {"ptpt", RootWoW::LocaleFlags::ptPT},
};

// Constructor: load & parse the "root" file
RootInstance::RootInstance(const std::string& path, const Settings& settings) : m_loadedWith(settings.RootMode) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open " + path);

    const bool fullMode = (settings.RootMode == RootWoW::LoadMode::Full);

    auto fileSize = std::filesystem::file_size(path);
    m_data.resize(fileSize);

    f.read((char*)(m_data.data()), fileSize);

    // 2) Wrap in DataReader
    DataReader dr(m_data.data(), m_data.size());

    // 3) Parse optional DF header
    uint32_t header    = dr.ReadInt32LE();
    bool     newRoot   = false;
    uint32_t dfVersion = 0;

    int totalFiles = 0;
    int namedFiles = 0;

    if (header == 1296454484u) {
        totalFiles = dr.ReadInt32LE();
        namedFiles = dr.ReadInt32LE();

        if (namedFiles == 1 || namedFiles == 2) {
            uint32_t dfHeaderSize = totalFiles;
            dfVersion = namedFiles;
            totalFiles = dr.ReadInt32LE();
            namedFiles  = dr.ReadInt32LE();
            dr.SetOffset(dfHeaderSize);
        } else {
            dr.SetOffset(12);
        }

        if (fullMode) {
            entriesFDIDFull.reserve(totalFiles);
        } else {
            entriesFDID.reserve(totalFiles);
        }

        if (namedFiles) {
            entriesLookup.reserve(namedFiles);
        }

        newRoot = true;
    }

    const size_t rootLen = m_data.size();

    // 4) Read each chunk/block
    while (dr.GetOffset() < rootLen) {
        uint32_t count = dr.ReadInt32LE();

        RootWoW::ContentFlags contentFlags;
        RootWoW::LocaleFlags  localeFlags;

        if (dfVersion == 2) {
            localeFlags  = static_cast<RootWoW::LocaleFlags>(dr.ReadInt32LE());
            uint32_t u1  = dr.ReadInt32LE();
            uint32_t u2  = dr.ReadInt32LE();
            uint8_t  b   = dr.ReadUInt8();
            contentFlags = static_cast<RootWoW::ContentFlags>(u1 | u2 | (uint32_t(b) << 17));
        } else {
            contentFlags = static_cast<RootWoW::ContentFlags>(dr.ReadInt32LE());
            localeFlags  = static_cast<RootWoW::LocaleFlags>(dr.ReadInt32LE());
        }

        bool localeSkip =
            !has_all(localeFlags, RootWoW::LocaleFlags::All_WoW) && !has_any(localeFlags, settings.Locale);

        bool contentSkip =
            (settings.preferLowViolence   && !has_any(contentFlags, RootWoW::ContentFlags::LowViolence)) ||
            (settings.preferHiResTextures && !has_any(contentFlags, RootWoW::ContentFlags::HighResTexture));

        bool skipChunk   = localeSkip || contentSkip;
        if (fullMode) skipChunk = false;

        bool separateLookup = newRoot;
        bool doLookup       = !newRoot || (totalFiles > 0 && totalFiles == namedFiles) ||
                              !has_any(contentFlags, RootWoW::ContentFlags::NoNames);

        // strides
        const int sizeFdid    = 4;
        const int sizeCHash   = 16;
        const int sizeLookup  = 8;

        size_t blockStart = dr.GetOffset();
        size_t blockSize  = count * (sizeFdid + sizeCHash + (doLookup ? sizeLookup : 0));

        auto drCopy = dr;
        auto combinedKeys = drCopy.sliceAndAdvance(sizeCHash * count + (doLookup ? sizeLookup : 0) * count);

        auto fileDataDeltas = dr.sliceAndAdvance(sizeFdid * count);
        auto cKeys = dr.sliceAndAdvance(sizeCHash * count);
        auto nameLookups = doLookup ? dr.sliceAndAdvance(sizeLookup * count) : DataReader(nullptr,0,0);

        if (!separateLookup) {
            cKeys = combinedKeys;
            nameLookups = combinedKeys;
        }

        if (!skipChunk) {
            uint32_t fileIndex = 0;

            for (uint32_t i = 0; i < count; ++i) {
                RootEntry entry{};
                entry.contentFlags = contentFlags;
                entry.localeFlags  = localeFlags;

                // — read file-data ID delta
                uint32_t fid  = fileIndex + fileDataDeltas.ReadInt32LE();
                entry.fileDataID = fid;
                fileIndex        = fid + 1;

                for (int k = 0; k < 16; ++k)
                    entry.md5[k] = cKeys.ReadUInt8();

                // — optional 64-bit lookup
                if (doLookup) {
                    entry.lookup = nameLookups.ReadUInt64LE();
                    entriesLookup.emplace(entry.lookup, entry.fileDataID);
                }

                if (fullMode)
                    entriesFDIDFull[entry.fileDataID].push_back(entry);
                else {
                    entriesFDID.emplace(entry.fileDataID, entry);
                }
            }
        }
    }
}

RootInstance::~RootInstance() {
//    std::cout << "RootInstance destroyed" << std::endl;
}

// Query methods
vector<RootInstance::RootEntry> RootInstance::GetEntriesByFDID(uint32_t id) const {
    if (m_loadedWith == RootWoW::LoadMode::Normal) {
        auto it = entriesFDID.find(id);
        if (it != entriesFDID.end()) return { it->second };
    } else {
        auto it = entriesFDIDFull.find(id);
        if (it != entriesFDIDFull.end()) return it->second;
    }
    return {};
}

vector<RootInstance::RootEntry> RootInstance::GetEntriesByLookup(uint64_t lk) const {
    auto it = entriesLookup.find(lk);
    if (it != entriesLookup.end()) return
        GetEntriesByFDID(it->second);
    return {};
}

vector<uint32_t> RootInstance::GetAvailableFDIDs() const {
    vector<uint32_t> out;
    if (m_loadedWith == RootWoW::LoadMode::Normal) {
        out.reserve(entriesFDID.size());
        for (auto const& kv : entriesFDID) out.push_back(kv.first);
    } else {
        out.reserve(entriesFDIDFull.size());
        for (auto const& kv : entriesFDIDFull) out.push_back(kv.first);
    }
    return out;
}

vector<uint64_t> RootInstance::GetAvailableLookups() const {
    vector<uint64_t> out;
    out.reserve(entriesLookup.size());
    for (auto const& kv : entriesLookup) out.push_back(kv.first);
    return out;
}

bool RootInstance::FileExists(uint64_t lk) const {
    return entriesLookup.find(lk) != entriesLookup.end();
}

bool RootInstance::FileExists(uint32_t id) const {
    if (m_loadedWith == RootWoW::LoadMode::Normal)
        return entriesFDID.find(id) != entriesFDID.end();
    else
        return entriesFDIDFull.find(id) != entriesFDIDFull.end();
}
