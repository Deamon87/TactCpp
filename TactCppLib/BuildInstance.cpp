// BuildInstance.cpp
#include "BuildInstance.h"

#include <filesystem>
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <ranges>
#include <generator>

#include "GroupIndex.h"
#include "utils/stringUtils.h"
#include "BuildInfo.h"
#include "utils/TactConfigParser.h"
#include "utils/Jenkins96.h"
#include "utils/json.hpp"

namespace fs = std::filesystem;
using namespace TACTLibUtils;

BuildInstance::BuildInstance(const Settings &settings): settings_(settings) {
}

BuildInstance::~BuildInstance() {
//    std::cout << "BuildInstance destroyed" << std::endl << std::flush;
}

void BuildInstance::LoadVersionInfo() {
    if (settings_.BaseDir.has_value()) {
        fs::path bp = fs::path(settings_.BaseDir.value()) / ".build.info";
        if (!fs::exists(bp)) throw std::runtime_error("No .build.info in basedir");
        BuildInfo bi(bp.string(), settings_, *cdn_);


        auto matchedEntries = bi.Entries | std::views::filter([l_product = settings_.Product](const auto &rng) {
            return rng.Product == l_product;
        }) | std::views::take(1) | std::ranges::to<std::vector>();

        if (matchedEntries.empty())
            throw std::runtime_error("No build found for product " + settings_.Product +
                                     " in .build.info, are you sure this product is installed?");

        auto const &buildInfoEntry = matchedEntries[0];
        if (settings_.BuildConfigPathOrHash.empty())
            settings_.BuildConfigPathOrHash = buildInfoEntry.BuildConfig;

        if (settings_.CDNConfigPathOrHash.empty())
            settings_.CDNConfigPathOrHash = buildInfoEntry.CDNConfig;

        if (settings_.ArmadilloKey.empty())
            settings_.ArmadilloKey = buildInfoEntry.Armadillo;

        if (settings_.CDNPath.empty())
            settings_.CDNPath = buildInfoEntry.CDNPath;
    }

    if ((!settings_.BaseDir.has_value() || settings_.BaseDir->empty()) && !settings_.useTactLocal) {
        auto versions = cdn_->GetPatchServiceFile(settings_.Product, "versions");
        TactConfigParser::parse(versions, {"Region", "BuildConfig", "CDNConfig", "ProductConfig"}, [&](const auto &rec) {
            if (settings_.Region != rec.at("Region")) { return true;} // continue if region do no match

            if (settings_.BuildConfigPathOrHash.empty())
                settings_.BuildConfigPathOrHash = rec.at("BuildConfig");

            if (settings_.CDNConfigPathOrHash.empty())
                settings_.CDNConfigPathOrHash = rec.at("CDNConfig");

            if (settings_.ProductConfigHash.empty())
                settings_.ProductConfigHash = rec.at("ProductConfig");

            return false;
        });
    }
}

void BuildInstance::LoadConfigs() {
    auto &buildConfigPath = settings_.BuildConfigPathOrHash;
    auto &cdnConfigPath = settings_.CDNConfigPathOrHash;

    auto start = std::chrono::steady_clock::now();

    // BuildConfig
    if (!settings_.BuildConfig.empty()) {
        buildConfig_ = std::make_unique<TactConfig>(settings_.BuildConfig);
    } else if (fs::exists(buildConfigPath)) {
        buildConfig_ = std::make_unique<TactConfig>(*cdn_, buildConfigPath, true);
    } else if (buildConfigPath.size() == 32 && std::all_of(buildConfigPath.begin(), buildConfigPath.end(), ::isxdigit)) {
        buildConfig_ = std::make_unique<TactConfig>(*cdn_, buildConfigPath, false);
    }

    // CDNConfig
    if (!settings_.CDNConfig.empty()) {
        cdnConfig_ = std::make_unique<TactConfig>(settings_.CDNConfig);
    } else if (fs::exists(cdnConfigPath)) {
        cdnConfig_ = std::make_unique<TactConfig>(*cdn_, cdnConfigPath, true);
    } else if (cdnConfigPath.size() == 32
               && std::all_of(cdnConfigPath.begin(), cdnConfigPath.end(), ::isxdigit)) {
        cdnConfig_ = std::make_unique<TactConfig>(*cdn_, cdnConfigPath, false);
    }

    if (!buildConfig_ || !cdnConfig_)
        throw std::runtime_error("Failed to load configs");

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::cout << "Configs loaded in " << std::ceil(ms) << "ms\n";
}

void BuildInstance::Load() {
    cdn_ = std::make_unique<CDN>(settings_);

    LoadVersionInfo();

    if (!settings_.CDNPath.empty())
        cdn_->setProductDirectory(settings_.CDNPath);

    // if a local base dir is set, switch CDN to local
    if (settings_.BaseDir.has_value()) {
        cdn_->OpenLocal();
    } else if (!settings_.useTactLocal) {
        {
            auto prodConfigData = cdn_->GetFile("prodConfig", settings_.ProductConfigHash);
            auto j = nlohmann::json::parse(prodConfigData.begin(), prodConfigData.end());
            auto jsonPtr = nlohmann::json::json_pointer("/all/config/decryption_key_name");

            if (j.contains(jsonPtr)) {
                settings_.ArmadilloKey = j[jsonPtr];
            }
        }
    }
    if (!settings_.ArmadilloKey.empty()) {
        cdn_->setArmadilloKey(settings_.ArmadilloKey);
        std::cout << "Set Armadillo key name to " + settings_.ArmadilloKey << std::endl;
    }

    LoadConfigs();

    if (!buildConfig_ || !cdnConfig_)
        throw std::runtime_error("Configs not loaded");

    // --- Group index ---
    auto t0 = std::chrono::steady_clock::now();
    auto &cdnVals = cdnConfig_->Values;
    auto itGroup = cdnVals.find("archive-group");
    if (itGroup == cdnVals.end()) {
        std::cout << "No group index found in CDN config, generating fresh group index...\n";
        GroupIndex newGen;
        auto hash = newGen.Generate(cdn_, settings_, "", cdnConfig_->Values.at("archives"));
        auto path = fs::path(settings_.CacheDir) / cdn_->ProductDirectory() / "data" / (hash + ".index");

        groupIndex_ = std::make_unique<IndexInstance>(path.string());
    } else {
        const auto &grp = itGroup->second;
        fs::path idxOnDisk = fs::path(settings_.BaseDir.value_or("")) / "Data" / "indices" / (grp[0] + ".index");
        if (settings_.BaseDir.has_value() && fs::exists(idxOnDisk)) {
            groupIndex_ = std::make_unique<IndexInstance>(idxOnDisk.string());
        } else {
            auto idxCache =
                fs::path(settings_.CacheDir.string()) / cdn_->ProductDirectory() / "data" / (grp[0] + ".index");
            if (!fs::exists(idxCache)) {
                GroupIndex regen;
                regen.Generate(cdn_, settings_, grp[0], cdnConfig_->Values.at("archives"));
            }
            groupIndex_ = std::make_unique<IndexInstance>(idxCache.string());
        }
    }
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();
        std::cout << "Group index loaded in " << std::ceil(ms) << "ms\n";
    }

    // --- File index ---
    t0 = std::chrono::steady_clock::now();
    auto itFile = cdnConfig_->Values.find("file-index");
    if (itFile == cdnConfig_->Values.end())
        throw std::runtime_error("No file index found in CDN config");

    const auto &fileIdx = itFile->second;
    fs::path fileOnDisk = fs::path(settings_.BaseDir.value_or(""))
                          / "Data"
                          / "indices"
                          / (fileIdx[0] + ".index");
    if (!settings_.BaseDir.has_value() && fs::exists(fileOnDisk)) {
        fileIndex_ = std::make_unique<IndexInstance>(fileOnDisk.string());
    } else {
        auto p = cdn_->GetFilePath("data", fileIdx[0] + ".index");
        fileIndex_ = std::make_unique<IndexInstance>(p);
    }
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();
        std::cout << "File index loaded in " << std::ceil(ms) << "ms\n";
    }

    // --- Encoding ---
    auto encSize = std::stoull(buildConfig_->Values.at("encoding-size")[0]);
    t0 = std::chrono::steady_clock::now();
    auto encPath = cdn_->GetDecodedFilePath(
        "data",
        buildConfig_->Values.at("encoding")[1],
        std::stoull(buildConfig_->Values.at("encoding-size")[1]),
        encSize
    );
    encoding_ = std::make_unique<TACTSharp::EncodingInstance>(encPath, static_cast<int>(encSize));
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();
        std::cout << "Encoding loaded in " << std::ceil(ms) << "ms\n";
    }

    // --- ListFile ---
    if (settings_.loadListFile) {
        listFile_ = std::make_unique<ListFile>(settings_);
    }

    // --- Root ---
    t0 = std::chrono::steady_clock::now();
    auto itRoot = buildConfig_->Values.find("root");
    if (itRoot == buildConfig_->Values.end())
        throw std::runtime_error("No root key found in build config");

    auto rootBytes = hexToBytes(itRoot->second[0]);
    auto rootKeys = encoding_->FindContentKey(rootBytes);
    if (rootKeys.empty())  // assumes implicit operator!()
        throw std::runtime_error("Root key not found in encoding");

    auto rootHex = bytesToHexLower(rootKeys.key(0));
    root_ = std::make_unique<RootInstance>(
        cdn_->GetDecodedFilePath("data", rootHex, 0, rootKeys.decodedFileSize),
        settings_,
        listFile_
    );
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();
        std::cout << "Root loaded in " << std::ceil(ms) << "ms\n";
    }

    // --- Install ---
    t0 = std::chrono::steady_clock::now();
    auto itInst = buildConfig_->Values.find("install");
    if (itInst == buildConfig_->Values.end())
        throw std::runtime_error("No install key found in build config");

    auto instBytes = hexToBytes(itInst->second[0]);
    auto instKeys = encoding_->FindContentKey(instBytes);
    if (instKeys.empty())
        throw std::runtime_error("Install key not found in encoding");

    auto instHex = bytesToHexLower(instKeys.key(0));
    install_ = std::make_unique<InstallInstance>(
        cdn_->GetDecodedFilePath("data", instHex, 0, instKeys.decodedFileSize)
    );
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0
        ).count();
        std::cout << "Install loaded in " << std::ceil(ms) << "ms\n";
    }
}

std::vector<uint8_t> arr16ToVec(const std::array<uint8_t, 16> &arr16) {
    return std::vector<uint8_t>(arr16.begin(), arr16.end());
}

std::vector<uint8_t> BuildInstance::OpenFileByFDID(uint32_t fileDataID) {
    if (!root_)
        throw std::runtime_error("Root not loaded");

    auto entries = root_->GetEntriesByFDID(fileDataID);
    if (entries.empty())
        throw std::runtime_error("File "+std::to_string(fileDataID) + " not found in root");

    return OpenFileByCKey(arr16ToVec(entries[0].md5));
}

std::vector<uint8_t> BuildInstance::OpenFileByName(const std::string &fileName) {
    // Normalize path separators
    std::string normName = fileName;
    std::replace(normName.begin(), normName.end(), '/', '\\');

    if (!root_)
        throw std::runtime_error("Root not loaded");

    std::vector<uint8_t> ckey;

    // Check for install_ and get CKey by name first (priority)
    bool foundCKeyInInstall = install_ && install_->getCKeyByName(fileName, ckey);
    if (!foundCKeyInInstall) {
        uint64_t hash = Jenkins96::ComputeHash(normName, true);

        auto entries = root_->GetEntriesByLookup(hash);
        if (entries.empty())
            throw std::runtime_error("File \"" + fileName + "\" with lookup " + std::to_string(hash) +
                                     " not found in root");

        ckey = std::vector<uint8_t>(entries[0].md5.begin(), entries[0].md5.end());
    }

    return OpenFileByCKey(ckey);
}

std::vector<uint8_t> BuildInstance::OpenFileByCKey(const std::string &cKey) {
    return OpenFileByCKey(hexToBytes(cKey));
}

std::vector<uint8_t> BuildInstance::OpenFileByCKey(const std::vector<uint8_t> &cKey) {
    if (!encoding_)
        throw std::runtime_error("Encoding not loaded");

    auto encRes = encoding_->FindContentKey(cKey);
    if (encRes.empty())
        throw std::runtime_error("File not found in encoding");

    return OpenFileByEKey(encRes.key(0), encRes.decodedFileSize);
}

std::vector<uint8_t> BuildInstance::OpenFileByEKey(const std::string &eKey, uint64_t decodedSize) {
    return OpenFileByEKey(hexToBytes(eKey), decodedSize);
}

std::vector<uint8_t> BuildInstance::OpenFileByEKey(const std::vector<uint8_t> &eKey, uint64_t decodedSize) {
    if (!groupIndex_ || !fileIndex_)
        throw std::runtime_error("Indexes not loaded");

    auto [offset, size, archiveIdx] = groupIndex_->GetIndexInfo(eKey);

    if (offset == -1) {
        // Not found in group index, check file index
        auto [fileOffset, fileSize, arcIdx] = fileIndex_->GetIndexInfo(eKey);

        //Data from FileIndex should not have these fields set, when returned from IndexInstance class
        assert(arcIdx == -1);
        assert(fileOffset == -1);

        if (fileSize == -1) {
            std::cout << "Warning: EKey " << bytesToHexLower(eKey)
                      << " not found in group or file index and might not be available on CDN." << std::endl;
            fileSize = 0;
        }

        return cdn_->GetFile("data", bytesToHexLower(eKey), fileSize, decodedSize, true);
    } else {
        // Found in group index - get from archive
        return cdn_->GetFileFromArchive(
            bytesToHexLower(eKey),
            cdnConfig_->Values.at("archives")[archiveIdx],
            offset,
            size,
            decodedSize,
            true
        );
    }
}