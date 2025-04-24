#ifndef BUILDINFO_H
#define BUILDINFO_H

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include "Settings.h"
#include "CDN.h"

// Represents a single available build entry
struct AvailableBuild {
    std::string Branch;
    std::string BuildConfig;
    std::string CDNConfig;
    std::string CDNPath;
    std::string KeyRing;
    std::string Version;
    std::string Product;
    std::string Folder;
    std::string Armadillo;
};

// Parses build info files and populates entries
class BuildInfo {
public:
    std::vector<AvailableBuild> Entries;

    BuildInfo(const std::string& path, const Settings& settings, CDN& cdn) {
        std::unordered_map<std::string, uint8_t> headerMap;
        std::unordered_map<std::string, std::string> folderMap;

        // Scan for .flavor.info files to build folderMap
        for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(settings.BaseDir.value())) {
            if (!dirEntry.is_regular_file()) continue;
            const auto& filePath = dirEntry.path();
            if (filePath.string().ends_with(".flavor.info")) {
                std::vector<std::string> flavorLines;
                std::ifstream flavorFile(filePath);
                std::string line;
                while (std::getline(flavorFile, line)) {
                    flavorLines.push_back(line);
                }
                if (flavorLines.size() < 2) continue;
                // Map product (line 2) to folder name
                folderMap[flavorLines[1]] = filePath.parent_path().filename().string();
            }
        }

        // Read main build info file
        std::ifstream infile(path);
        std::string line;
        while (std::getline(infile, line)) {
            auto splitLine = split(line, '|');
            if (!splitLine.empty() && splitLine[0] == "Branch!STRING:0") {
                // Header row: map column names to indices
                for (size_t i = 0; i < splitLine.size(); ++i) {
                    const auto& header = splitLine[i];
                    auto pos = header.find('!');
                    std::string key = (pos != std::string::npos) ? header.substr(0, pos) : header;
                    headerMap[key] = static_cast<uint8_t>(i);
                }
                continue;
            }

            if (headerMap.empty()) continue;

            AvailableBuild ab;
            ab.BuildConfig = splitLine[headerMap["Build Key"]];
            ab.CDNConfig   = splitLine[headerMap["CDN Key"]];
            ab.CDNPath     = splitLine[headerMap["CDN Path"]];
            ab.Version     = splitLine[headerMap["Version"]];
            ab.Armadillo   = splitLine[headerMap["Armadillo"]];
            ab.Product     = splitLine[headerMap["Product"]];

            auto itKeyRing = headerMap.find("KeyRing");
            if (itKeyRing != headerMap.end()) {
                ab.KeyRing = splitLine[itKeyRing->second];
            }

            auto itCDNHosts = headerMap.find("CDN Hosts");
            if (itCDNHosts != headerMap.end()) {
                auto hosts = split(splitLine[itCDNHosts->second], ' ');
                cdn.SetCDNs(hosts);
            }

            auto itFolder = folderMap.find(ab.Product);
            if (itFolder != folderMap.end()) {
                ab.Folder = itFolder->second;
            } else {
                std::cout << "No flavor found matching " << ab.Product << std::endl;
            }

            Entries.push_back(std::move(ab));
        }
    }

private:
    // Utility to split strings on a character delimiter
    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }
};


#endif //BUILDINFO_H
