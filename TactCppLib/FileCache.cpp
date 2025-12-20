#include "FileCache.h"
#include <fstream>
#include <filesystem>

FileCache::FileCache(const std::filesystem::path& cacheDir) {
    cacheDir_ = cacheDir;
}

std::filesystem::path FileCache::GetCachePath(
    const std::string& key,
    const std::string& fileType,
    const std::string& archive,
    const std::string& productDirectory) const {
    
    std::string actualFileType = archive.empty() ? fileType : "data";
    std::filesystem::path cacheDir = cacheDir_ / productDirectory / actualFileType;
    return cacheDir / key;
}

std::vector<uint8_t> FileCache::TryLoadFromCache(
    const std::string& key,
    const std::string& fileType,
    const std::string& archive,
    const std::string& productDirectory,
    uint64_t expectedSize) {
    
    std::filesystem::path cachePath = GetCachePath(key, fileType, archive, productDirectory);

    // Check cache validity
    if (std::filesystem::exists(cachePath)) {
        auto size = std::filesystem::file_size(cachePath);
        bool valid = (expectedSize == 0 || size == expectedSize);

        if (valid) {
            std::scoped_lock<std::mutex> lock(fileLocks_[cachePath.string()]);
            std::vector<uint8_t> buf(size);
            std::ifstream in(cachePath, std::ios::binary);
            in.read(reinterpret_cast<char*>(buf.data()), buf.size());
            return buf;
        } else {
            std::filesystem::remove(cachePath);
        }
    }
    
    return std::vector<uint8_t>();
}

void FileCache::SaveToCache(
    const std::string& key,
    const std::string& fileType,
    const std::string& archive,
    const std::string& productDirectory,
    const std::vector<uint8_t>& data) {
    
    std::filesystem::path cachePath = GetCachePath(key, fileType, archive, productDirectory);

    std::scoped_lock lock(fileLocks_[cachePath.string()]);
    std::filesystem::create_directories(cachePath.parent_path());
    std::ofstream out(cachePath, std::ios::binary);
    out.write((const char*)(data.data()), data.size());
    out.close();
}

