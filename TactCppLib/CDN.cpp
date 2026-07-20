#include "CDN.h"
#include "utils/KeyService.h"
#include "utils/SalsaDecrypt.h"
#include "utils/stringUtils.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <ranges>
#include <format>

#ifndef __ANDROID__
#include "cpr/cpr.h"
#endif

using namespace TACTLibUtils;

bool readRawFile(const std::string& path, std::vector<uint8_t>& buffer) {
    // Check if file exists on disk
    if (!std::filesystem::exists(path)) {
        return false;
    }

    // Open the file in binary mode, and position the read pointer at the end
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cout << "Unable to open file: " + path << std::endl << std::flush;
        return false;
    }

    // Get size and allocate vector
    std::streamsize size = file.tellg();
    buffer.resize(size);

    // Seek back to beginning and read all bytes
    file.seekg(0, std::ios::beg);
    if (!file.read((char*)(buffer.data()), size)) {
        return false;
    }

    file.close();

    return true;
}

CDN::CDN(const Settings &settings) : settings_(settings) {

    auto fileCache  = std::make_unique<FileCache>(settings_.CacheDir);
    fileCache_ = std::move(fileCache);

    armadilloKey_ = settings.ArmadilloKey;
}

CDN::~CDN() {
//    std::cout << "CDN destroyed" << std::endl;
}

void CDN::OpenLocal() {
    if (!settings_.BaseDir.has_value())
        return;

    try {
        auto start = std::chrono::steady_clock::now();
        LoadCASCIndices();
        hasLocalCasc = true;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "Loaded local CASC indices in " << elapsed << "ms" << std::endl << std::flush;
    } catch (const std::exception &e) {
        std::cerr << "Failed to load CASC indices: " << e.what() << std::endl << std::flush;;
    }
}

void CDN::SetCDNs(const std::vector<std::string> &cdns) {
    std::lock_guard<std::mutex> lock(cdnSettingMutex_);
    for (auto &url: cdns) {
        if (std::find(cdnServers_.begin(), cdnServers_.end(), url) == cdnServers_.end())
            cdnServers_.push_back(url);
    }
}

std::string DownloadTextFromURL(const std::string &url) {
    auto r = cpr::Get(cpr::Url(url));
    if (r.status_code != 200) {
        std::cout << "Failed to download " << url << " (code: "<< r.status_code << ")" << std::endl;
        return "";
    }

    return r.text;
}

std::string CDN::GetPatchServiceFile(const std::string &product, const std::string &file) {
    std::string url = std::format("https://us.version.battle.net/{}/{}", product, file);
    return DownloadTextFromURL(url);
}

std::vector<uint8_t> CDN::GetFile(const std::string &type,
                                  const std::string &hash,
                                  uint64_t compressedSize,
                                  uint64_t decompressedSize,
                                  bool decode) {

    auto data = DownloadFile(type, hash, "", 0, compressedSize);
    if (!decode)
        return data;
    return BLTE::Decode(data, decompressedSize);
}

std::vector<uint8_t> CDN::GetFileFromArchive(const std::string &eKey,
                                             const std::string &archive,
                                             size_t offset,
                                             size_t length,
                                             uint64_t decompressedSize,
                                             bool decode) {
    std::atomic<bool> cancel{false};
    auto data = DownloadFile("", eKey, archive, offset, length, cancel);
    if (!decode)
        return data;
    return BLTE::Decode(data, decompressedSize);
}

std::string CDN::GetFilePath(const std::string &type, const std::string &hash, uint64_t compressedSize) {

    std::filesystem::path cache = settings_.CacheDir / productDirectory_ / type / hash;

    if (std::filesystem::exists(cache)) {
        if (compressedSize > 0 && std::filesystem::file_size(cache) != compressedSize)
            std::filesystem::remove(cache);
        else
            return cache.string();
    }

    std::atomic<bool> cancel{false};
    auto data = DownloadFile(type, hash, "", 0, compressedSize, cancel);
    std::filesystem::create_directories(cache.parent_path());
    std::ofstream ofs(cache, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(data.data()), data.size());
    return cache.string();
}

std::string CDN::GetDecodedFilePath(const std::string &type,
                                    const std::string &hash,
                                    uint64_t compressedSize,
                                    uint64_t decompressedSize) {
    std::filesystem::path path = settings_.CacheDir / productDirectory_ / type / (hash + ".decoded");
    if (std::filesystem::exists(path))
        return path.string();

    auto data = DownloadFile(type, hash, "", 0, compressedSize);
    auto decoded = BLTE::Decode(data, decompressedSize);
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(decoded.data()), decoded.size());
    return path.string();
}

void CDN::LoadCDNs() {
    if (onlineCDNsLoaded) return;
    std::scoped_lock lock(cdnLoadingMutex_);
    if (onlineCDNsLoaded) return;

    if (!settings_.CDNServersOverride.empty()) {
        auto servers = tokenizeAndFilter(settings_.CDNServersOverride, " ",
                                          [](std::string &s) { return !s.empty(); });
        SetCDNs(servers);
        onlineCDNsLoaded = true;
        return;
    }

    if ((!productDirectory_.empty() && !cdnServers_.empty()) || settings_.useTactLocal) return;

    auto start = std::chrono::steady_clock::now();

    std::string url = std::format("http://{}.patch.battle.net:1119/{}/cdns", settings_.Region, settings_.Product);

    auto r = cpr::Get(cpr::Url(url));
    if (r.status_code != 200) {
        return ;
    }

    std::string cdnText = DownloadTextFromURL(url);

    auto lines = tokenizeAndFilter(cdnText, "\n", [](std::string &line) {
        if (line.empty()) return false;
        if (startsWith(line, "##")) return false;
        return true;
    });

    int NameIndex = -1;
    int PathIndex = -1;
    int HostsIndex = -1;
    auto headerTokens = tokenize(lines[0], "|");
    for (int i = 0 ; i < headerTokens.size(); i++) {
        if (startsWith(headerTokens[i], "Name")) {
            NameIndex = i;
        }
        if (startsWith(headerTokens[i], "Path")) {
            PathIndex = i;
        }
        if (startsWith(headerTokens[i], "Hosts")) {
            HostsIndex = i;
        }
    }

    if (NameIndex != -1 && PathIndex != -1 && HostsIndex != -1) {
        for (auto &line: lines) {
            auto recordTokens = tokenize(line, "|");

            if (recordTokens[NameIndex] != settings_.Region) continue;

            if (productDirectory_.empty())
                setProductDirectory(recordTokens[PathIndex]);

            auto servers = tokenize(recordTokens[HostsIndex], " ");
            SetCDNs(servers);
        }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "Loaded and sorted CDNs in " << elapsed << "ms" << std::endl << std::flush;;

    onlineCDNsLoaded = true;
}

void CDN::LoadCASCIndices() {
    if (!settings_.BaseDir.has_value()) return;

    std::filesystem::path dataDir = settings_.BaseDir.value();
    dataDir /= "Data/data";

    if (!std::filesystem::exists(dataDir)) return;

    for (auto &entry: std::filesystem::directory_iterator(dataDir)) {
        if (entry.path().extension() != ".idx") continue;

        auto name = entry.path().stem().string();
        if (name.rfind("tempfile", 0) == 0) continue;

        uint8_t bucket = std::stoul(name.substr(0, 2), nullptr, 16);
        cascIndices_.emplace(bucket, std::make_unique<CASCIndexInstance>(entry.path().string()));
    }
}

std::vector<uint8_t> CDN::DownloadFile(
    const std::string& type,
    const std::string& key,
    const std::string& archive,
    int offset,
    uint64_t expectedSize,
    int timeoutMs)
{
    // 1) Attempt local fetch
    if (hasLocalCasc) {
        try {
            std::vector<uint8_t> data;

            // Determine the path based on type and key
            std::filesystem::path p;

            if (archive.empty()) {
                // Original local resolution logic for data/config
                if (type == "data" && key.rfind(".index") == key.size() - 6) {
                    p = std::filesystem::path(settings_.BaseDir.value_or("")) / "Data" / "indices" / key;
                } else if (type == "config" && key.size() >= 4) {
                    p = std::filesystem::path(settings_.BaseDir.value_or("")) / "Data" / "config" /
                        key.substr(0,2) / key.substr(2,2) / key;
                }
            }

            // Check if the path is valid and exists
            if (!p.empty() && settings_.BaseDir.has_value() && readRawFile(p.string(), data)) {
                return data;
            } else if (TryGetLocalFile(key, data)) {
                return data;
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to read local file: " << e.what() << std::endl;
        }
    }

    // Ensure CDN list is loaded (so that the productDirectory is filled)
    if (productDirectory_.empty()) {
        LoadCDNs();
    }

    // 2) Try to load from cache
    auto cachedData = fileCache_->TryLoadFromCache(key, type, archive, productDirectory_, expectedSize);
    if (!cachedData.empty()) {
        return cachedData;
    }

    if (!settings_.allowOnlineDownload) {
         throw std::runtime_error("Failed to load " + key + " file from local ");
    }

    // 4) Try load data from CDN either way (function has prevention from double load)
    {
        LoadCDNs();
    }

    // 5) Get file from CDN(s)
    auto data = std::vector<uint8_t>();
    if (settings_.useTactLocal) {
        data = DownloadFileFromLocal(type, key, archive, offset, expectedSize);
    }
    if (data.empty()) {
        data = DownloadFileFromHTTP(type, key, archive, offset, expectedSize, timeoutMs);
    }

    return data;
}

std::vector<uint8_t> CDN::DownloadFileFromHTTP(
    const std::string& type,
    const std::string& key,
    const std::string& archive,
    int offset,
    uint64_t expectedSize,
    int timeoutMs)
{
    std::string fileType = archive.empty() ? type : "data";
    for (const auto& server : cdnServers_) {
        // URL segments
        std::string seg1 = archive.empty() ? key.substr(0,2) : archive.substr(0,2);
        std::string seg2 = archive.empty() ? key.substr(2,2) : archive.substr(2,2);
        std::string resource = archive.empty() ? key : archive;

        std::string url = fileType == "prodConfig" ?
            std::format("http://{}/{}/{}/{}/{}", server, "tpr/configs/data", seg1, seg2, resource) :
            std::format("http://{}/{}/{}/{}/{}/{}", server, productDirectory_, fileType, seg1, seg2, resource);

        if (!archive.empty()) {
            std::cout << "Downloading chunk " << key << " from " << url <<
                " (offset " << offset << " expected size " << expectedSize << " )" << std::endl << std::flush;
        } else {
            std::cout << "Downloading " << key << " from " << url << "(expected size " << expectedSize << " )" << std::endl << std::flush;
        }

        // Build request
        cpr::Session session;
        session.SetUrl(cpr::Url{url});
        if (timeoutMs > 0)
            session.SetTimeout(cpr::Timeout{timeoutMs});
        if (!archive.empty()) {
            std::string rangeHeader = std::to_string(offset) + "-" + std::to_string(offset + expectedSize - 1);
            session.SetHeader({{"Range", "bytes=" + rangeHeader}});
        }
        auto fileSize = expectedSize;//session.GetDownloadFileLength();

        size_t readOfs = 0;
        std::vector<uint8_t> resultFile(fileSize > 0 ? fileSize : 0);

        cpr::Response r = session.Download(cpr::WriteCallback([&](const std::string &data, intptr_t userdata) -> bool {
            if (resultFile.size() < (readOfs + data.size())) resultFile.resize(readOfs + data.size());

            std::copy(data.begin(), data.end(), resultFile.begin() + readOfs);

            readOfs += data.size();
            return true;
        }, 0));

        if (r.status_code == 200 || r.status_code == 206) {
            // Write to cache

            std::array<uint8_t, 16> decodeKey;
            if (fileType != "prodConfig" && !armadilloKey_.empty() && KeyService::TryGetArmadilloKey(armadilloKey_, decodeKey)) {
                SalsaDecrypt::Decrypt(resultFile, offset, resource.substr(16, 16), decodeKey);
            }

            fileCache_->SaveToCache(key, type, archive, productDirectory_, resultFile);

            return std::move(resultFile);
        }

        std::cerr << "HTTP " << r.status_code << " downloading " << key << " from " << server << '\n';
    }

    throw std::runtime_error(
        archive.empty()
        ? "Exhausted all CDNs trying to download " + key
        : "Exhausted all CDNs trying to download " + key + " (archive " + archive + ")");
}

std::vector<uint8_t> CDN::DownloadFileFromLocal(
    const std::string& type,
    const std::string& key,
    const std::string& archive,
    int offset,
    uint64_t expectedSize)
{
    std::string fileType = archive.empty() ? type : "data";
    
    // URL segments (same as HTTP version, but without server)
    std::string seg1 = archive.empty() ? key.substr(0,2) : archive.substr(0,2);
    std::string seg2 = archive.empty() ? key.substr(2,2) : archive.substr(2,2);
    std::string resource = archive.empty() ? key : archive;

    // Build local path: {productDirectory_}/{fileType}/{seg1}/{seg2}/{resource}
    std::filesystem::path localPath = std::filesystem::path(*settings_.LocalTactPath) / productDirectory_/ fileType / seg1 / seg2 / resource;

    if (!std::filesystem::exists(localPath)) {
        throw std::runtime_error(
            archive.empty()
            ? "File not found locally: " + localPath.string()
            : "Archive not found locally: " + localPath.string());
    }

    std::ifstream file(localPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open local file: " + localPath.string());
    }

    // Determine what to read
    size_t readSize = expectedSize;
    if (archive.empty()) {
        // For full files, read the entire file if size not specified, otherwise read expectedSize
        if (readSize == 0) {
            file.seekg(0, std::ios::end);
            readSize = file.tellg();
            file.seekg(0, std::ios::beg);
        }
    } else {
        // For archive chunks, read the specified range starting at offset
        file.seekg(offset, std::ios::beg);
        if (readSize == 0) {
            // If size not specified, read to end of file
            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(offset, std::ios::beg);
            readSize = fileSize - offset;
        }
    }

    std::vector<uint8_t> resultFile(readSize);
    if (!file.read(reinterpret_cast<char*>(resultFile.data()), readSize)) {
        // Check if we read some data before the error
        size_t bytesRead = file.gcount();
        if (bytesRead > 0 && bytesRead < readSize) {
            resultFile.resize(bytesRead);
        } else {
            throw std::runtime_error("Failed to read from local file: " + localPath.string());
        }
    } else {
        // Resize if we read less than expected
        size_t bytesRead = file.gcount();
        if (bytesRead < readSize) {
            resultFile.resize(bytesRead);
        }
    }

    std::array<uint8_t, 16> decodeKey;
    if (fileType != "prodConfig" && !armadilloKey_.empty() && KeyService::TryGetArmadilloKey(armadilloKey_, decodeKey)) {
        SalsaDecrypt::Decrypt(resultFile, offset, resource.substr(16, 16), decodeKey);
    }

    // Dont write to cache when reading from local
    //fileCache_->SaveToCache(key, type, archive, productDirectory_, resultFile);

    return resultFile;
}

std::string PadLeft(const std::string& input, std::size_t totalLength, char symbol) {
    if (input.size() >= totalLength) {
        return input;  // no padding needed
    }
    // create a string of (totalLength - input.size()) zeros, then append input
    return std::string(totalLength - input.size(), symbol) + input;
}

bool CDN::TryGetLocalFile(const std::string &eKey, std::vector<uint8_t> &outData) {
    auto bytes = hexToBytes(eKey);

    uint8_t i = 0;
    for (int idx = 0; idx < 9; ++idx) i ^= bytes[idx];

    uint8_t bucket = (i & 0xF) ^ (i >> 4);

    auto it = cascIndices_.find(bucket);
    if (it == cascIndices_.end()) return false;

    auto info = it->second->GetIndexInfo(bytes);
    if (info.archiveOffset == -1) return false;

    std::filesystem::path archivePath =
        settings_.BaseDir.value() / ("Data/data/data." + PadLeft(std::to_string(info.archiveIndex), 3, '0'));

    size_t fileLen = std::filesystem::file_size(archivePath);
    if (info.archiveOffset + info.archiveSize > fileLen) return false;
    std::ifstream ifs(archivePath, std::ios::binary);
    ifs.seekg(info.archiveOffset);
    outData.resize(info.archiveSize);
    ifs.read(reinterpret_cast<char *>(outData.data()), info.archiveSize);
    return true;
}
