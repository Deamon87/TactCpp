#include "CDN.h"
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

    file.close();

    return buffer;
}




CDN::CDN(const Settings &settings)
    : settings_(settings) {
}

void CDN::OpenLocal() {
    if (settings_.BaseDir.has_value())
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

std::string DownloadTextFromURL(const std::string &url) {
    auto r = cpr::Get(cpr::Url(url));
    if (r.status_code != 200) {
        std::cout << "Failed to download " << url << " (code: "<< r.status_code << ")" << std::endl;
        return "";
    }

    return r.text;
}

std::string CDN::GetPatchServiceFile(const std::string &product, const std::string &file) {
    std::string url = std::format("https://{}.version.battle.net/{}/{}", settings_.Region, product, file);
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
    auto path = GetFilePath(type, hash + ".decoded", compressedSize);
    if (std::filesystem::exists(path))
        return path;

    auto data = DownloadFile(type, hash, "", 0, compressedSize);
    auto decoded = BLTE::Decode(data, decompressedSize);
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char *>(decoded.data()), decoded.size());
    return path;
}

void CDN::LoadCDNs() {
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

            auto servers = tokenize(recordTokens[HostsIndex], " ");
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
    if (!settings_.BaseDir.has_value()) return;

    std::filesystem::path dataDir = settings_.BaseDir.value();
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

std::vector<uint8_t> CDN::DownloadFile(
    const std::string& type,
    const std::string& key,
    const std::string& archive,
    int offset,
    uint64_t expectedSize,
    int timeoutMs)
{
    // 1) Attempt local fetch
    if (hasLocal_) {
        try {
            std::vector<uint8_t> data;
            if (archive.empty()) {
                // Original local resolution logic for data/config
                if (type == "data" && key.rfind(".index") == key.size() - 6) {
                    std::filesystem::path p = std::filesystem::path(settings_.BaseDir.value()) / "Data" / "indices" / key;
                    if (std::filesystem::exists(p)) {
                        return readFile(p.string());
                    }
                } else if (type == "config" && key.size() >= 4) {
                    std::filesystem::path p =
                        std::filesystem::path(settings_.BaseDir.value()) / "Data" / "config" /
                            key.substr(0,2) / key.substr(2,2) / key;
                    if (std::filesystem::exists(p)) {
                        return readFile(p.string());
                    }
                } else if (TryGetLocalFile(key, data)) {
                    return data;
                }
            } else {
                // Archive-based local lookup
                if (TryGetLocalFile(key, data)) {
                    return data;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to read local file: " << e.what() << '\n';
        }
    }

    // 2) Prepare cache path
    std::string fileType = archive.empty() ? type : "data";
    std::filesystem::path cacheDir = std::filesystem::path(settings_.CacheDir) / productDirectory_ / fileType;
    std::filesystem::path cachePath = cacheDir / key;

    // Ensure a lock exists for this path
    fileLocks_.try_emplace(cachePath.string());

    // 3) Check cache validity
    if (std::filesystem::exists(cachePath)) {
        auto size = std::filesystem::file_size(cachePath);
        bool valid = archive.empty()
            ? (expectedSize == 0 || size == expectedSize)
            : (static_cast<uint64_t>(size) >= static_cast<uint64_t>(offset) + expectedSize);
        if (valid) {
            std::scoped_lock<std::mutex> lock(*fileLocks_[cachePath.string()]);
            std::vector<uint8_t> buf(size);
            std::ifstream in(cachePath, std::ios::binary);
            in.read(reinterpret_cast<char*>(buf.data()), buf.size());
            return buf;
        } else {
            std::filesystem::remove(cachePath);
        }
    }

    // 4) Ensure CDN list is loaded
    {
        std::scoped_lock lock(cdnMutex_);
        if (cdnServers_.empty()) LoadCDNs();
    }

    // 5) Download from CDN(s)
    for (const auto& server : cdnServers_) {
        // URL segments
        std::string seg1 = archive.empty() ? key.substr(0,2) : archive.substr(0,2);
        std::string seg2 = archive.empty() ? key.substr(2,2) : archive.substr(2,2);
        std::string resource = archive.empty() ? key : archive;

        std::string url = std::format("http://{}/{}/{}/{}/{}/{}", server, productDirectory_, fileType, seg1, seg2, resource);

        std::cout << "Downloading " << key << " from " << url << '\n';

        // Build request
        cpr::Session session;
        session.SetUrl(cpr::Url{url});
        if (timeoutMs > 0)
            session.SetTimeout(cpr::Timeout{timeoutMs});
        if (!archive.empty()) {
            std::string rangeHeader = std::to_string(offset) + "-" + std::to_string(offset + expectedSize - 1);
            session.SetHeader({{"Range", "bytes=" + rangeHeader}});
        }

        cpr::Response r = session.Get();
        if (r.status_code == 200) {
            // Write to cache
            if (archive.empty()) {
                std::scoped_lock lock(*fileLocks_[cachePath.string()]);
                std::filesystem::create_directories(cacheDir);
                std::ofstream out(cachePath, std::ios::binary);
                out.write(r.text.c_str(), r.text.size());
            }

            // Return data
            std::vector<uint8_t> buf(r.text.begin(), r.text.end());
            return buf;
        }

        std::cerr << "HTTP " << r.status_code << " downloading " << key << " from " << server << '\n';
    }

    throw std::runtime_error(
        archive.empty()
        ? "Exhausted all CDNs trying to download " + key
        : "Exhausted all CDNs trying to download " + key + " (archive " + archive + ")");
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
    if (info.archiveOffset == (size_t) -1) return false;

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
