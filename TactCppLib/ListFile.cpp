//
// Created for ListFile support
//

#include "ListFile.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

#ifndef __ANDROID__
#include "cpr/cpr.h"
#endif

ListFile::ListFile(const Settings& settings) {
    // Check if ListFile should be loaded
    if (!settings.loadListFile) {
        return;
    }

    // Try to load from local file first if path is provided
    if (settings.ListfilePath.has_value()) {
        if (LoadFromLocalFile(settings.ListfilePath.value())) {
            m_loaded = true;
            return;
        }
    }

    // If local file failed or not provided, try to download from URL
    if (!settings.ListfileURL.empty()) {
        if (LoadFromURL(settings.ListfileURL)) {
            m_loaded = true;
            return;
        }
    }
}

bool ListFile::LoadFromLocalFile(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        std::cout << "ListFile: Local file does not exist: " << path << std::endl;
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "ListFile: Cannot open local file: " << path << std::endl;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    ParseListFile(content);
    std::cout << "ListFile: Loaded " << m_fileMap.size() << " entries from local file: " << path << std::endl;
    return true;
}

bool ListFile::LoadFromURL(const std::string& url) {
#ifndef __ANDROID__
    std::cout << "ListFile: Downloading from URL: " << url << std::endl;

    try {
        auto verSSL = cpr::VerifySsl{false};
        auto r = cpr::Get(cpr::Url{url}, verSSL);

        if (r.status_code == 200) {
            ParseListFile(r.text);
            std::cout << "ListFile: Loaded " << m_fileMap.size() << " entries from URL: " << url << std::endl;
            return true;
        } else {
            std::cout << "ListFile: Failed to download from URL. Status code: " << r.status_code << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cout << "ListFile: Exception while downloading from URL: " << e.what() << std::endl;
        return false;
    }
#else
    std::cout << "ListFile: HTTP download not supported on Android" << std::endl;
    return false;
#endif
}

void ListFile::ParseListFile(const std::string& content) {
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Find first semicolon
        size_t semicolonPos = line.find(';');
        if (semicolonPos == std::string::npos) {
            // No semicolon found, skip this line
            continue;
        }

        // Extract number (before semicolon)
        std::string numberStr = line.substr(0, semicolonPos);
        // Extract filename (after semicolon)
        std::string filename = line.substr(semicolonPos + 1);

        // Try to parse the number
        try {
            uint32_t fileDataID = std::stoul(numberStr);
            // Only insert if filename is not empty
            if (!filename.empty()) {
                m_fileMap[fileDataID] = filename;
            }
        } catch (const std::exception&) {
            // Failed to parse number, skip this line
            continue;
        }
    }
}

std::string ListFile::GetFilename(uint32_t fileDataID) const {
    auto it = m_fileMap.find(fileDataID);
    if (it != m_fileMap.end()) {
        return it->second;
    }
    return "";
}

