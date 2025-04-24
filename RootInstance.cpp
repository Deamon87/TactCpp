#include "RootInstance.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>

using namespace std;

// Helpers to read little-endian integers
uint32_t RootInstance::ReadUInt32LE(const vector<uint8_t>& data, size_t offset) {
    return uint32_t(data[offset])
         | (uint32_t(data[offset+1]) << 8)
         | (uint32_t(data[offset+2]) << 16)
         | (uint32_t(data[offset+3]) << 24);
}

uint64_t RootInstance::ReadUInt64LE(const vector<uint8_t>& data, size_t offset) {
    uint64_t lo = ReadUInt32LE(data, offset);
    uint64_t hi = ReadUInt32LE(data, offset + 4);
    return lo | (hi << 32);
}

// Constructor: load & parse the "root" file
RootInstance::RootInstance(const string& path, const Settings& settings)
  : m_loadedWith(settings.RootMode)
{
    // Read entire file into memory
    auto fileSize = filesystem::file_size(path);
    m_data.resize(fileSize);
    ifstream f(path, ios::binary);
    if (!f) throw runtime_error("Cannot open " + path);
    f.read(reinterpret_cast<char*>(m_data.data()), fileSize);

    // Parse header
    size_t offset = 0;
    uint32_t header = ReadUInt32LE(m_data, 0);
    bool newRoot = false;
    uint32_t dfVersion = 0;

    if (header == 1296454484u) {
        uint32_t totalFiles = ReadUInt32LE(m_data, 4);
        uint32_t namedFiles = ReadUInt32LE(m_data, 8);
        if (namedFiles == 1 || namedFiles == 2) {
            uint32_t dfHeaderSize = totalFiles;
            dfVersion = namedFiles;
            totalFiles = ReadUInt32LE(m_data, 12);
            namedFiles = ReadUInt32LE(m_data, 16);
            offset = dfHeaderSize;
        } else {
            offset = 12;
        }
        newRoot = true;
    }

    const bool fullMode = (settings.RootMode == RootWoW::LoadMode::Full);
    const size_t rootLen = m_data.size();

    while (offset < rootLen) {
        uint32_t count = ReadUInt32LE(m_data, offset);
        offset += 4;

        RootWoW::ContentFlags contentFlags;
        RootWoW::LocaleFlags  localeFlags;

        if (dfVersion == 2) {
            localeFlags  = static_cast<RootWoW::LocaleFlags>(ReadUInt32LE(m_data, offset)); offset += 4;
            uint32_t u1  = ReadUInt32LE(m_data, offset); offset += 4;
            uint32_t u2  = ReadUInt32LE(m_data, offset); offset += 4;
            uint8_t b    = m_data[offset++];

            contentFlags = static_cast<RootWoW::ContentFlags>(u1 | u2 | (uint32_t(b) << 17));
        } else {
            contentFlags = static_cast<RootWoW::ContentFlags>(ReadUInt32LE(m_data, offset)); offset += 4;
            localeFlags  = static_cast<RootWoW::LocaleFlags>(ReadUInt32LE(m_data, offset)); offset += 4;
        }

        bool localeSkip  = !( (static_cast<uint32_t>(localeFlags) & static_cast<uint32_t>(RootWoW::LocaleFlags::All_WoW)) ||
                              (static_cast<uint32_t>(localeFlags) & static_cast<uint32_t>(settings.Locale)) );
        bool contentSkip = (static_cast<uint32_t>(contentFlags) & static_cast<uint32_t>(RootWoW::ContentFlags::LowViolence)) != 0;
        bool skipChunk   = localeSkip || contentSkip;
        if (fullMode) skipChunk = false;

        bool separateLookup = newRoot;
        bool doLookup = !newRoot ||
                        ((static_cast<uint32_t>(contentFlags) & static_cast<uint32_t>(RootWoW::ContentFlags::NoNames)) == 0);

        int sizeFdid   = 4;
        int sizeCHash  = 16;
        int sizeLookup = 8;
        int strideFdid = sizeFdid;
        int strideCHash= separateLookup ? sizeCHash : sizeCHash + sizeLookup;
        int strideLookup = separateLookup ? sizeLookup : sizeCHash + sizeLookup;

        size_t offFdid    = offset;
        size_t offCHash   = offFdid + count * sizeFdid;
        size_t offLookup  = offCHash + (separateLookup ? count * sizeCHash : sizeCHash);
        size_t blockSize  = count * (sizeFdid + sizeCHash + (doLookup ? sizeLookup : 0));

        if (!skipChunk) {
            uint32_t fileIndex = 0;
            for (uint32_t i = 0; i < count; ++i) {
                RootEntry entry{};
                entry.contentFlags = contentFlags;
                entry.localeFlags  = localeFlags;

                uint32_t offs = ReadUInt32LE(m_data, offFdid);
                offFdid += strideFdid;
                uint32_t fid = fileIndex + offs;
                entry.fileDataID = fid;
                fileIndex = fid + 1;

                std::copy_n(entry.md5.data(), 16, m_data.data() + offCHash);

                offCHash += strideCHash;

                if (doLookup) {
                    entry.lookup = ReadUInt64LE(m_data, offLookup);
                    offLookup += strideLookup;
                    entriesLookup.emplace(entry.lookup, entry.fileDataID);
                }

                if (fullMode) {
                    entriesFDIDFull[entry.fileDataID].push_back(entry);
                } else {
                    entriesFDID.emplace(entry.fileDataID, entry);
                }
            }
        }

        offset += blockSize;
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
