#include "RootInstance.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>

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
RootInstance::RootInstance(const std::string& path, const Settings& settings)
  : m_loadedWith(settings.RootMode) {
    // 1) Read entire file
    auto fileSize = std::filesystem::file_size(path);
    m_data.resize(fileSize);
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open " + path);
    f.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    // 2) Wrap in DataReader
    DataReader dr(m_data.data(), m_data.size());

    // 3) Parse optional DF header
    uint32_t header    = dr.ReadInt32LE();
    bool     newRoot   = false;
    uint32_t dfVersion = 0;

    if (header == 1296454484u) {
        uint32_t totalFiles = dr.ReadInt32LE();
        uint32_t namedFiles = dr.ReadInt32LE();

        if (namedFiles == 1 || namedFiles == 2) {
            uint32_t dfHeaderSize = totalFiles;
            dfVersion = namedFiles;
            totalFiles = dr.ReadInt32LE();
            namedFiles  = dr.ReadInt32LE();
            dr.SetOffset(dfHeaderSize);
        } else {
            dr.SetOffset(12);
        }

        entriesFDID.reserve(totalFiles);
        newRoot = true;
    }

    const bool fullMode = (settings.RootMode == RootWoW::LoadMode::Full);
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

        bool localeSkip = !((uint32_t(localeFlags)  & uint32_t(RootWoW::LocaleFlags::All_WoW)) ||
                             (uint32_t(localeFlags) & uint32_t(settings.Locale)));
        bool contentSkip = (uint32_t(contentFlags) & uint32_t(RootWoW::ContentFlags::LowViolence)) != 0;
        bool skipChunk   = localeSkip || contentSkip;
        if (fullMode) skipChunk = false;

        bool separateLookup = newRoot;
        bool doLookup       = !newRoot ||
                              ((uint32_t(contentFlags) & uint32_t(RootWoW::ContentFlags::NoNames)) == 0);

        // strides
        const int sizeFdid    = 4;
        const int sizeCHash   = 16;
        const int sizeLookup  = 8;
        const int strideFdid  = sizeFdid;
        const int strideCHash = separateLookup ? sizeCHash : (sizeCHash + sizeLookup);
        const int strideLookup= separateLookup ? sizeLookup : (sizeCHash  + sizeLookup);

        // compute offsets within this block
        size_t blockStart = dr.GetOffset();
        size_t offFdid    = blockStart;
        size_t offCHash   = offFdid  + count * sizeFdid;
        size_t offLookup  = offCHash + (separateLookup ? count * sizeCHash : sizeCHash);
        size_t blockSize  = count * (sizeFdid + sizeCHash + (doLookup ? sizeLookup : 0));

        if (!skipChunk) {
            uint32_t fileIndex = 0;

            for (uint32_t i = 0; i < count; ++i) {
                RootEntry entry{};
                entry.contentFlags = contentFlags;
                entry.localeFlags  = localeFlags;

                // — read file-data ID delta
                dr.SetOffset(offFdid + i * strideFdid);
                uint32_t offs = dr.ReadInt32LE();
                uint32_t fid  = fileIndex + offs;
                entry.fileDataID = fid;
                fileIndex       = fid + 1;

                // — read 16-byte MD5
                dr.SetOffset(offCHash + i * strideCHash);
                for (int k = 0; k < 16; ++k)
                    entry.md5[k] = dr.ReadUInt8();

                // — optional 64-bit lookup
                if (doLookup) {
                    dr.SetOffset(offLookup + i * strideLookup);
                    entry.lookup = dr.ReadUInt64LE();
                    entriesLookup.emplace(entry.lookup, entry.fileDataID);
                }

                if (fullMode)
                    entriesFDIDFull[entry.fileDataID].push_back(entry);
                else
                    entriesFDID.emplace(entry.fileDataID, entry);
            }
        }

        // advance past the entire block
        dr.SetOffset(blockStart + blockSize);
    }
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
    if (it != entriesLookup.end()) return GetEntriesByFDID(it->second);
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
