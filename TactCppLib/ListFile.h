//
// Created for ListFile support
//

#ifndef LISTFILE_H
#define LISTFILE_H

#include <string>
#include <unordered_map>
#include <optional>
#include <filesystem>
#include "Settings.h"

class ListFile {
public:
    explicit ListFile(const Settings& settings);
    ~ListFile() = default;

    // Get filename by fileDataID (number), returns empty string if not found
    std::string GetFilename(uint32_t fileDataID) const;

    // Check if ListFile is loaded and ready
    bool IsLoaded() const { return m_loaded; }

private:
    bool LoadFromLocalFile(const std::filesystem::path& path);
    bool LoadFromURL(const std::string& url);
    void ParseListFile(const std::string& content);

    std::unordered_map<uint32_t, std::string> m_fileMap;
    bool m_loaded = false;
};

#endif //LISTFILE_H

