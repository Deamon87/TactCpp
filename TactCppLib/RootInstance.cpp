#include "RootInstance.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <memory>
#include <map>
#include <set>

#include "utils/DataReader.h"
#include "wow/WoWRootFlags.h"
#include "ListFile.h"

using namespace std;

// #define DUMP_ROOT
#ifdef DUMP_ROOT
class RootDumper {
private:
    std::ofstream m_file;
    uint32_t m_chunkIndex;
    const std::unique_ptr<ListFile> &m_listFile;
    RootWoW::ContentFlags m_currentContentFlags;
    
    // Data for flag deduction
    std::map<std::string, std::set<uint32_t>> m_extensionToFlags;  // extension -> set of content flags
    std::map<uint32_t, std::set<std::string>> m_flagToExtensions;  // content flag -> set of extensions
    std::map<std::string, uint32_t> m_extensionFileCount;          // extension -> count of files
    std::map<uint32_t, uint32_t> m_flagFileCount;                  // content flag -> count of files
    
    static std::string ParseContentFlags(RootWoW::ContentFlags flags) {
        std::ostringstream oss;
        uint32_t value = static_cast<uint32_t>(flags);
        
        if (value == 0) {
            oss << "None";
            return oss.str();
        }
        
        bool first = true;
        if (value & 0x1) { if (!first) oss << " | "; oss << "HighResTexture"; first = false; }
        if (value & 0x2) { if (!first) oss << " | "; oss << "Install"; first = false; }
        if (value & 0x4) { if (!first) oss << " | "; oss << "F00000004"; first = false; }
        if (value & 0x8) { if (!first) oss << " | "; oss << "LoadOnWindows"; first = false; }
        if (value & 0x10) { if (!first) oss << " | "; oss << "LoadOnMacOS"; first = false; }
        if (value & 0x80) { if (!first) oss << " | "; oss << "LowViolence"; first = false; }
        if (value & 0x100) { if (!first) oss << " | "; oss << "DoNotLoad"; first = false; }
        if (value & 0x800) { if (!first) oss << " | "; oss << "UpdatePlugin"; first = false; }
        if (value & 0x8000000) { if (!first) oss << " | "; oss << "Encrypted"; first = false; }
        if (value & 0x10000000) { if (!first) oss << " | "; oss << "NoNames"; first = false; }
        if (value & 0x20000000) { if (!first) oss << " | "; oss << "UncommonRes"; first = false; }
        if (value & 0x40000000) { if (!first) oss << " | "; oss << "Bundle"; first = false; }
        if (value & 0x80000000) { if (!first) oss << " | "; oss << "NoCompression"; first = false; }
        
        // Handle unknown bits
        uint32_t knownBits = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x80 | 0x100 | 0x800 | 0x8000000 | 0x10000000 | 0x20000000 | 0x40000000 | 0x80000000;
        uint32_t unknownBits = value & ~knownBits;
        if (unknownBits != 0) {
            // Log each unknown bit individually
            for (int bit = 0; bit < 32; ++bit) {
                uint32_t bitMask = 1u << bit;
                if (unknownBits & bitMask) {
                    if (!first) oss << " | ";
                    oss << "UnknownBit" << bit << "(0x" << std::hex << bitMask << std::dec << ")";
                    first = false;
                }
            }
        }
        
        return oss.str();
    }
    
    static std::string ParseLocaleFlags(RootWoW::LocaleFlags flags) {
        std::ostringstream oss;
        uint32_t value = static_cast<uint32_t>(flags);
        
        if (value == 0) {
            oss << "None";
            return oss.str();
        }
        
        bool first = true;
        if (value & 0x1) { if (!first) oss << " | "; oss << "Unk_1"; first = false; }
        if (value & 0x2) { if (!first) oss << " | "; oss << "enUS"; first = false; }
        if (value & 0x4) { if (!first) oss << " | "; oss << "koKR"; first = false; }
        if (value & 0x8) { if (!first) oss << " | "; oss << "Unk_8"; first = false; }
        if (value & 0x10) { if (!first) oss << " | "; oss << "frFR"; first = false; }
        if (value & 0x20) { if (!first) oss << " | "; oss << "deDE"; first = false; }
        if (value & 0x40) { if (!first) oss << " | "; oss << "zhCN"; first = false; }
        if (value & 0x80) { if (!first) oss << " | "; oss << "esES"; first = false; }
        if (value & 0x100) { if (!first) oss << " | "; oss << "zhTW"; first = false; }
        if (value & 0x200) { if (!first) oss << " | "; oss << "enGB"; first = false; }
        if (value & 0x400) { if (!first) oss << " | "; oss << "enCN"; first = false; }
        if (value & 0x800) { if (!first) oss << " | "; oss << "enTW"; first = false; }
        if (value & 0x1000) { if (!first) oss << " | "; oss << "esMX"; first = false; }
        if (value & 0x2000) { if (!first) oss << " | "; oss << "ruRU"; first = false; }
        if (value & 0x4000) { if (!first) oss << " | "; oss << "ptBR"; first = false; }
        if (value & 0x8000) { if (!first) oss << " | "; oss << "itIT"; first = false; }
        if (value & 0x10000) { if (!first) oss << " | "; oss << "ptPT"; first = false; }
        if (value & 0x20000000) { if (!first) oss << " | "; oss << "enSG"; first = false; }
        if (value & 0x40000000) { if (!first) oss << " | "; oss << "plPL"; first = false; }
        
        // Handle unknown bits
        uint32_t knownBits = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000 | 0x10000 | 0x20000000 | 0x40000000;
        uint32_t unknownBits = value & ~knownBits;
        if (unknownBits != 0) {
            // Log each unknown bit individually
            for (int bit = 0; bit < 32; ++bit) {
                uint32_t bitMask = 1u << bit;
                if (unknownBits & bitMask) {
                    if (!first) oss << " | ";
                    oss << "UnknownBit" << bit << "(0x" << std::hex << bitMask << std::dec << ")";
                    first = false;
                }
            }
        }
        
        return oss.str();
    }
    
public:
    explicit RootDumper(const std::string& filePath, const std::unique_ptr<ListFile> &listFile)
        : m_chunkIndex(0), m_listFile(listFile) {
        m_file.open(filePath, std::ios::out | std::ios::trunc);
        if (!m_file.is_open()) {
            throw std::runtime_error("Cannot open dump file: " + filePath);
        }
    }
    
    void DeduceUnknownContentFlags() {
        m_file << std::endl;
        m_file << "========================================" << std::endl;
        m_file << "=== CONTENT FLAG DEDUCTION ANALYSIS ===" << std::endl;
        m_file << "========================================" << std::endl;
        m_file << std::endl;
        
        // Find exclusive flag-extension associations
        std::map<uint32_t, std::string> deducedFlags;
        
        for (const auto& [flagValue, extensions] : m_flagToExtensions) {
            if (extensions.size() == 1) {
                // This flag only appears with one extension
                const std::string& ext = *extensions.begin();
                
                // Check if this extension only appears with this flag
                const auto& flagsForExt = m_extensionToFlags.find(ext);
                if (flagsForExt != m_extensionToFlags.end() && flagsForExt->second.size() == 1) {
                    deducedFlags[flagValue] = ext;
                }
            }
        }
        
        m_file << "Exclusive flag-extension associations found:" << std::endl;
        m_file << std::endl;
        
        if (deducedFlags.empty()) {
            m_file << "  No exclusive associations found." << std::endl;
        } else {
            for (const auto& [flagValue, extension] : deducedFlags) {
                m_file << "  ContentFlag 0x" << std::hex << std::setw(8) << std::setfill('0') 
                       << flagValue << std::dec 
                       << " (" << ParseContentFlags(static_cast<RootWoW::ContentFlags>(flagValue)) << ")" 
                       << std::endl;
                m_file << "    <-> Extension: ." << extension << std::endl;
                m_file << "    Files with this extension: " << m_extensionFileCount[extension] << std::endl;
                m_file << "    Files with this flag: " << m_flagFileCount[flagValue] << std::endl;
                m_file << std::endl;
            }
        }
        
        m_file << std::endl;
        m_file << "========================================" << std::endl;
        m_file << "=== PARTIAL ASSOCIATIONS (ANALYSIS) ===" << std::endl;
        m_file << "========================================" << std::endl;
        m_file << std::endl;
        m_file << "Extensions and their associated content flags:" << std::endl;
        m_file << std::endl;
        
        for (const auto& [extension, flags] : m_extensionToFlags) {
            m_file << "  ." << extension << " (files: " << m_extensionFileCount.at(extension) << ")" << std::endl;
            
            // Calculate AND of all flags for this extension
            uint32_t flagsAnd = 0xFFFFFFFF;
            for (uint32_t flagValue : flags) {
                flagsAnd &= flagValue;
            }
            
            m_file << "    AND of all flags: 0x" << std::hex << std::setw(8) << std::setfill('0') 
                   << flagsAnd << std::dec;
            if (flagsAnd != 0) {
                m_file << " (" << ParseContentFlags(static_cast<RootWoW::ContentFlags>(flagsAnd)) << ")";
            } else {
                m_file << " (None)";
            }
            m_file << std::endl;
            
            m_file << "    Individual flags:" << std::endl;
            for (uint32_t flagValue : flags) {
                m_file << "      - 0x" << std::hex << std::setw(8) << std::setfill('0') 
                       << flagValue << std::dec 
                       << " (" << ParseContentFlags(static_cast<RootWoW::ContentFlags>(flagValue)) << ")"
                       << " [" << m_flagFileCount.at(flagValue) << " files total]" << std::endl;
            }
            m_file << std::endl;
        }
        
        m_file << std::endl;
        m_file << "Content flags and their associated extensions:" << std::endl;
        m_file << std::endl;
        
        for (const auto& [flagValue, extensions] : m_flagToExtensions) {
            m_file << "  0x" << std::hex << std::setw(8) << std::setfill('0') 
                   << flagValue << std::dec 
                   << " (" << ParseContentFlags(static_cast<RootWoW::ContentFlags>(flagValue)) << ")"
                   << " [" << m_flagFileCount.at(flagValue) << " files]" << std::endl;
            for (const std::string& ext : extensions) {
                m_file << "    - ." << ext << " [" << m_extensionFileCount.at(ext) << " files]" << std::endl;
            }
            m_file << std::endl;
        }
    }
    
    ~RootDumper() {
        if (m_file.is_open()) {
            DeduceUnknownContentFlags();
            m_file.close();
        }
    }
    
    void StartChunk(RootWoW::ContentFlags contentFlags, RootWoW::LocaleFlags localeFlags) {
        m_currentContentFlags = contentFlags;
        m_file << std::endl;
        m_file << "=== Chunk " << m_chunkIndex << " ===" << std::endl;
        m_file << "ContentFlags: 0x" << std::hex << std::setw(8) << std::setfill('0')
               << static_cast<uint32_t>(contentFlags) << std::dec 
               << " (" << ParseContentFlags(contentFlags) << ")" << std::endl;
        m_file << "LocaleFlags: 0x" << std::hex << std::setw(8) << std::setfill('0') 
               << static_cast<uint32_t>(localeFlags) << std::dec 
               << " (" << ParseLocaleFlags(localeFlags) << ")" << std::endl;
        m_file << std::endl;
        m_chunkIndex++;
    }
    
    void LogEntry(const RootInstance::RootEntry &entry) {
        m_file << "  FDID: " << entry.fileDataID;
        if (entry.lookup > 0) {
            m_file << " lookup: 0x"
               << std::hex
               << std::uppercase
               << std::setfill('0')
               << std::setw(16)
               << entry.lookup
               << std::dec
               << " ";
        }
        if (m_listFile && m_listFile->IsLoaded()) {
            std::string filename = m_listFile->GetFilename(entry.fileDataID);
            if (!filename.empty()) {
                m_file << " (" << filename << ")";
                
                // Collect data for flag deduction
                size_t dotPos = filename.find_last_of('.');
                if (dotPos != std::string::npos && dotPos < filename.length() - 1) {
                    std::string extension = filename.substr(dotPos + 1);
                    // Convert to lowercase for consistency
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                    
                    uint32_t contentFlagValue = static_cast<uint32_t>(m_currentContentFlags);
                    
                    m_extensionToFlags[extension].insert(contentFlagValue);
                    m_flagToExtensions[contentFlagValue].insert(extension);
                    m_extensionFileCount[extension]++;
                    m_flagFileCount[contentFlagValue]++;
                }
            }
        }
        m_file << std::endl;
    }
};
#endif // DUMP_ROOT

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
RootInstance::RootInstance(const std::string& path, const Settings& settings, const std::unique_ptr<ListFile> &listFile) : m_loadedWith(settings.RootMode) {
#ifdef DUMP_ROOT
    RootDumper dumper(path + ".txt", listFile);
#endif // DUMP_ROOT
    
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
    } else {
        // Old layout has no header/magic: the 4 bytes consumed above by the
        // magic-check read actually belong to the first block's `count` field.
        // Rewind so the chunk-parsing loop below sees them.
        dr.SetOffset(0);
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
            (!settings.preferLowViolence   && has_any(contentFlags, RootWoW::ContentFlags::LowViolence)) ||
            (!settings.preferHiResTextures && has_any(contentFlags, RootWoW::ContentFlags::HighResTexture));

        bool skipChunk   = localeSkip || contentSkip;
        if (fullMode) skipChunk = false;

#ifdef DUMP_ROOT
        skipChunk = false;
        dumper.StartChunk(contentFlags, localeFlags);
#endif // DUMP_ROOT


        bool separateLookup = newRoot;
        bool doLookup       = !newRoot || (totalFiles > 0 && totalFiles == namedFiles) ||
                              !has_any(contentFlags, RootWoW::ContentFlags::NoNames);

        // strides
        const int sizeFdid    = 4;
        const int sizeCHash   = 16;
        const int sizeLookup  = 8;

        size_t blockStart = dr.GetOffset();
        size_t blockSize  = count * (sizeFdid + sizeCHash + (doLookup ? sizeLookup : 0));

        auto fileDataDeltas = dr.sliceAndAdvance(sizeFdid * count);

        auto drCopy = dr;
        auto combinedKeys = drCopy.sliceAndAdvance(sizeCHash * count + (doLookup ? sizeLookup : 0) * count);

        auto cKeysStorage = dr.sliceAndAdvance(sizeCHash * count);
        auto nameLookupsStorage = doLookup ? dr.sliceAndAdvance(sizeLookup * count) : DataReader(nullptr,0,0);

        // Old layout interleaves md5+lookup per entry (24-byte stride), so both
        // reads must share one cursor for the stride to line up correctly;
        // new layout keeps md5 and lookup in separate, independently-strided arrays.
        DataReader &cKeys       = separateLookup ? cKeysStorage       : combinedKeys;
        DataReader &nameLookups = separateLookup ? nameLookupsStorage : combinedKeys;

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

#ifdef DUMP_ROOT
                dumper.LogEntry(entry);
#endif // DUMP_ROOT

                if (fullMode)
                    entriesFDIDFull[entry.fileDataID].push_back(entry);
                else {
                    if (!localeSkip && !contentSkip && entriesFDID.find(entry.fileDataID) == entriesFDID.end())
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
