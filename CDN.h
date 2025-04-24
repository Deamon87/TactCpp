#ifndef CDN_H
#define CDN_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <future>
#include <cstdint>
#include "Settings.h"
#include "CASCIndexInstance.h"
#include "BLTE.h"

class CDN {
public:
    explicit CDN(const Settings& settings);

    // Load local CASC indices if available
    void OpenLocal();

    // Provide list of CDN server URLs
    void SetCDNs(const std::vector<std::string>& cdns);

    // Fetch from patch service
    std::string GetPatchServiceFile(const std::string& product, const std::string& file = "versions");

    // Get raw or decoded blob
    std::vector<uint8_t> GetFile(const std::string& type,
                                 const std::string& hash,
                                 uint64_t compressedSize = 0,
                                 uint64_t decompressedSize = 0,
                                 bool decode = false);

    std::vector<uint8_t> GetFileFromArchive(const std::string& eKey,
                                            const std::string& archive,
                                            size_t offset,
                                            size_t length,
                                            uint64_t decompressedSize = 0,
                                            bool decode = false);

    // Ensure on-disk copy, return path
    std::string GetFilePath(const std::string& type,
                            const std::string& hash,
                            uint64_t compressedSize = 0);

    std::string GetDecodedFilePath(const std::string& type,
                                   const std::string& hash,
                                   uint64_t compressedSize = 0,
                                   uint64_t decompressedSize = 0);

    // Flip product directory after LoadCDNs
    const std::string& ProductDirectory() const { return productDirectory_; }
    void setProductDirectory(const std::string& value) { productDirectory_ = value; }

private:
    void LoadCDNs();
    void LoadCASCIndices();

    std::vector<uint8_t> DownloadFile(
        const std::string& type,
        const std::string& key,
        const std::string& archive = "",
        int offset = 0,
        uint64_t expectedSize = 0,
        int timeoutMs = 0);

    bool TryGetLocalFile(const std::string& eKey, std::vector<uint8_t>& outData);

    std::vector<std::string> cdnServers_;
    std::unordered_map<std::string, std::mutex> fileLocks_;
    std::mutex cdnMutex_;
    bool hasLocal_ = false;
    std::unordered_map<uint8_t, std::unique_ptr<CASCIndexInstance>> cascIndices_;
    Settings settings_;
    std::string productDirectory_;
};

#endif //CDN_H
