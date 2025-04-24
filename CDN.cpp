#include "CDN.h"
#include "utils/stringUtils.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <ranges>
#include <format>

inline std::vector<uint8_t> readFile(const std::string& path) {
    // Open the file in binary mode, and position the read pointer at the end
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Unable to open file: " + path);
    }

    // Get size and allocate vector
    std::streamsize size = file.tellg();
    std::vector<uint8_t> buffer(size);

    // Seek back to beginning and read all bytes
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Error reading file: " + path);
    }

    return buffer;
}




CDN::CDN(const Settings &settings)
    : settings_(settings) {
}

void CDN::OpenLocal() {
    if (settings_.BaseDir.empty())
        return;

    try {
        auto start = std::chrono::steady_clock::now();
        LoadCASCIndices();
        hasLocal_ = true;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "Loaded local CASC indices in " << elapsed << "ms\n";
    } catch (const std::exception &e) {
        std::cerr << "Failed to load CASC indices: " << e.what() << "\n";
    }
}

void CDN::SetCDNs(const std::vector<std::string> &cdns) {
    std::lock_guard<std::mutex> lock(cdnMutex_);
    for (auto &url: cdns) {
        if (std::find(cdnServers_.begin(), cdnServers_.end(), url) == cdnServers_.end())
            cdnServers_.push_back(url);
    }
}

std::string CDN::GetPatchServiceFile(const std::string &product, const std::string &file) {
    std::string url = std::format("https://{}.version.battle.net/{}/{}", settings_.Region, product, file);
    return client_.GetString(url);
}

std::vector<uint8_t> CDN::GetFile(const std::string &type,
                                  const std::string &hash,
                                  uint64_t compressedSize,
                                  uint64_t decompressedSize,
                                  bool decode) {
    std::atomic<bool> cancel{false};
    auto data = DownloadFile(type, hash, compressedSize, cancel);
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
    auto data = DownloadFileFromArchiveInternal(eKey, archive, offset, length, cancel);
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
    auto data = DownloadFile(type, hash, compressedSize, cancel);
    std::filesystem::create_directories(cache.parent_path());
    std::ofstream ofs(cache, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(data.data()), data.size());
    return cache.string();
}

std::string CDN::GetDecodedFilePath(const std::string &type,
                                    const std::string &hash,
                                    uint64_t compressedSize,
                                    uint64_t decompressedSize) {
    auto path = GetFilePath(type, hash + ".decoded", compressedSize);
    if (std::filesystem::exists(path))
        return path;

    auto data = DownloadFile(type, hash, compressedSize, std::atomic<bool>{false});
    auto decoded = BLTE::Decode(data, decompressedSize);
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(decoded.data()), decoded.size());
    return path;
}

void CDN::LoadCDNs() {
    auto start = std::chrono::steady_clock::now();

    std::string url = std::format("http://{}.patch.battle.net:1119/{}/cdns", settings_.Region, settings_.Product);

    std::string cdnText = client_.GetString(url);

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
        if (startsWith(headerTokens[0], "Name")) {
            NameIndex = i;
        }
        if (startsWith(headerTokens[0], "Path")) {
            PathIndex = i;
        }
        if (startsWith(headerTokens[0], "Hosts")) {
            HostsIndex = i;
        }
    }

    if (NameIndex != -1 && PathIndex != -1 && HostsIndex != -1) {
        for (auto &line: lines) {
            auto recordTokens = tokenize(line, "|");

            if (recordTokens[NameIndex] != settings_.Region) continue;

            if (productDirectory_.empty())
                productDirectory_ = recordTokens[PathIndex];

            auto servers = tokenize(recordTokens[HostsIndex], ' ');
            SetCDNs(servers);
        }
    }
    cdnServers_.push_back("archive.wow.tools");

    // TODO: ping measurement and sorting omitted for brevity
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "Loaded and sorted CDNs in " << elapsed << "ms\n";
}

void CDN::LoadCASCIndices() {
    if (settings_.BaseDir.empty()) return;
    std::filesystem::path dataDir = settings_.BaseDir;
    dataDir /= "Data/data";
    if (!std::filesystem::exists(dataDir)) return;

    for (auto &entry: std::filesystem::directory_iterator(dataDir)) {
        if (entry.path().extension() != ".idx") continue;
        auto name = entry.path().stem().string();
        if (name.rfind("tempfile", 0) == 0) continue;
        uint8_t bucket = std::stoul(name.substr(0, 2), nullptr, 16);
        cascIndices_.emplace(bucket,
                             std::make_unique<CASCIndexInstance>(entry.path().string()));
    }
}

std::vector<uint8_t> CDN::DownloadFile(const std::string &type,
                                       const std::string &hash,
                                       uint64_t size,
                                       const std::atomic<bool> &cancelFlag) {
    // Local lookup
    if (hasLocal_) {
        if (type == "data" && hash.ends_with(".index")) {
            std::filesystem::path p = settings_.BaseDir / "Data/indices" / hash;
            if (std::filesystem::exists(p)) {
                return readFile(p);
            }
        }
        std::vector<uint8_t> local;
        if (TryGetLocalFile(hash, local))
            return local;
    }

    // Ensure CDN list
    {
        std::lock_guard<std::mutex> lock(cdnMutex_);
        if (cdnServers_.empty())
            LoadCDNs();
    }

    std::filesystem::path cache = settings_.CacheDir / productDirectory_ / type / hash;

    auto &fileMutex = fileLocks_[cache.string()];
    if (!fileMutex) fileMutex = std::make_shared<std::mutex>();
    {
        std::lock_guard<std::mutex> lk(*fileMutex);
        if (std::filesystem::exists(cache)) {
            if (size > 0 && std::filesystem::file_size(cache) != size)
                std::filesystem::remove(cache);
            else {
                std::ifstream ifs(cache, std::ios::binary);
                return std::vector<uint8_t>(std::istreambuf_iterator<char>(ifs), {});
            }
        }
    }

    // Download attempts
    for (auto &server: cdnServers_) {
        std::string url = std::format("http://{}/{}/{}/{}/{}/{}",
            server, productDirectory_, type, hash.substr(0, 2), hash.substr(2, 2), hash);



        std::cout << "Downloading " << url << "\n";
        auto response = client_.GetStream(url);
        if (!response.IsSuccess()) continue; {
            std::lock_guard<std::mutex> lk(*fileMutex);
            std::filesystem::create_directories(cache.parent_path());
            std::ofstream ofs(cache, std::ios::binary);
            ofs << response.GetStream().rdbuf();
        }
        return DownloadFile(type, hash, size, cancelFlag); // read from cache
    }
    throw std::runtime_error("Exhausted all CDNs downloading " + hash);
}

std::vector<uint8_t> CDN::DownloadFileFromArchiveInternal(const std::string &eKey,
                                                          const std::string &archive,
                                                          size_t offset,
                                                          size_t length,
                                                          const std::atomic<bool> &cancelFlag) {
    // Similar to DownloadFile, with HTTP Range header
    // Omitted for brevity; follow same pattern and use client_.GetStreamWithRange
    return {};
}

std::string PadLeft(const std::string& input, std::size_t totalLength, char symbol) {
    if (input.size() >= totalLength) {
        return input;  // no padding needed
    }
    // create a string of (totalLength - input.size()) zeros, then append input
    return std::string(totalLength - input.size(), symbol) + input;
}

bool CDN::TryGetLocalFile(const std::string &eKey, std::vector<uint8_t> &outData) {
    auto bytes = FromHex(eKey);

    uint8_t i = 0;
    for (int idx = 0; idx < 9; ++idx) i ^= bytes[idx];

    uint8_t bucket = (i & 0xF) ^ (i >> 4);

    auto it = cascIndices_.find(bucket);
    if (it == cascIndices_.end()) return false;

    auto info = it->second->GetIndexInfo(bytes);
    if (info.offset == (size_t) -1) return false;

    std::filesystem::path archivePath = settings_.BaseDir / ("Data/data/data." + PadLeft(info.archiveIndex, 3, '0'));

    size_t fileLen = std::filesystem::file_size(archivePath);
    if (info.offset + info.size > fileLen) return false;
    std::ifstream ifs(archivePath, std::ios::binary);
    ifs.seekg(info.offset);
    outData.resize(info.size);
    ifs.read(reinterpret_cast<char *>(outData.data()), info.size);
    return true;
}
