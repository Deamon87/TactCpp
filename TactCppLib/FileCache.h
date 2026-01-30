#ifndef FILECACHE_H
#define FILECACHE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include <cstdint>

class FileCache {
public:
    explicit FileCache(const std::filesystem::path& cacheDir);
    
    // Try to load file from cache. Returns empty vector if not found or invalid.
    std::vector<uint8_t> TryLoadFromCache(
        const std::string& key,
        const std::string& fileType,
        const std::string& archive,
        const std::string& productDirectory,
        uint64_t expectedSize);
    
    // Save file to cache.
    void SaveToCache(
        const std::string& key,
        const std::string& fileType,
        const std::string& archive,
        const std::string& productDirectory,
        const std::vector<uint8_t>& data);

private:
    std::filesystem::path GetCachePath(
        const std::string& key,
        const std::string& fileType,
        const std::string& archive,
        const std::string& productDirectory) const;

    std::filesystem::path cacheDir_;
    std::unordered_map<std::string, std::mutex> fileLocks_;
};

#endif // FILECACHE_H

