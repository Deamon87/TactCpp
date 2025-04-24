// BuildInstance.cpp
#include "BuildInstance.h"

#include <filesystem>
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>
#include "utils/stringUtils.h"

namespace fs = std::filesystem;

BuildInstance::BuildInstance()
  : settings_(std::make_shared<Settings>()),
    cdn_(std::make_shared<CDN>(*settings_))
{
}

void BuildInstance::LoadConfigs(const std::string& buildConfigPath,
                                const std::string& cdnConfigPath)
{
    settings_->SetBuildConfig(buildConfigPath);
    settings_->SetCDNConfig(cdnConfigPath);

    auto start = std::chrono::steady_clock::now();

    // BuildConfig
    if (fs::exists(buildConfigPath)) {
        buildConfig_ = std::make_shared<Config>(*cdn_, buildConfigPath, true);
    }
    else if (buildConfigPath.size() == 32
          && std::all_of(buildConfigPath.begin(), buildConfigPath.end(), ::isxdigit))
    {
        buildConfig_ = std::make_shared<Config>(*cdn_, buildConfigPath, false);
    }

    // CDNConfig
    if (fs::exists(cdnConfigPath)) {
        cdnConfig_ = std::make_shared<Config>(*cdn_, cdnConfigPath, true);
    }
    else if (cdnConfigPath.size() == 32
          && std::all_of(cdnConfigPath.begin(), cdnConfigPath.end(), ::isxdigit))
    {
        cdnConfig_ = std::make_shared<Config>(*cdn_, cdnConfigPath, false);
    }

    if (!buildConfig_ || !cdnConfig_)
        throw std::runtime_error("Failed to load configs");

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::cout << "Configs loaded in " << std::ceil(ms) << "ms\n";
}

void BuildInstance::Load()
{
    if (!buildConfig_ || !cdnConfig_)
        throw std::runtime_error("Configs not loaded");

    // if a local base dir is set, switch CDN to local
    if (!settings_->BaseDir.empty())
        cdn_->OpenLocal();

    // --- Group index ---
    auto t0 = std::chrono::steady_clock::now();
    auto cdnVals = cdnConfig_->GetValues();
    auto itGroup = cdnVals.find("archive-group");
    if (itGroup == cdnVals.end()) {
        std::cout << "No group index found in CDN config, generating fresh group index...\n";
        GroupIndex newGen;
        auto hash = newGen.Generate(*cdn_, *settings_, "", cdnConfig_->GetValues().at("archives"));
        auto path = fs::path(settings_->CacheDir) / cdn_->ProductDirectory() / "data"/ (hash + ".index");

        groupIndex_ = std::make_shared<IndexInstance>(path.string());
    }
    else {
        const auto& grp = itGroup->second;
        fs::path idxOnDisk = fs::path(settings_->BaseDir.value()) / "Data" / "indices" / (grp[0] + ".index");
        if (fs::exists(idxOnDisk)) {
            groupIndex_ = std::make_shared<IndexInstance>(idxOnDisk.string());
        }
        else {
            auto idxCache = fs::path(settings_->GetCacheDir())
                          / cdn_->GetProductDirectory()
                          / "data"
                          / (grp[0] + ".index");
            if (!fs::exists(idxCache)) {
                GroupIndex regen;
                regen.Generate(*cdn_, *settings_, grp[0], cdnConfig_->GetValues().at("archives"));
            }
            groupIndex_ = std::make_shared<IndexInstance>(idxCache.string());
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
    auto itFile = cdnConfig_->GetValues().find("file-index");
    if (itFile == cdnConfig_->GetValues().end())
        throw std::runtime_error("No file index found in CDN config");

    const auto& fileIdx = itFile->second;
    fs::path fileOnDisk = fs::path(settings_->GetBaseDir())
                        / "Data"
                        / "indices"
                        / (fileIdx[0] + ".index");
    if (!settings_->GetBaseDir().empty() && fs::exists(fileOnDisk)) {
        fileIndex_ = std::make_shared<IndexInstance>(fileOnDisk.string());
    }
    else {
        auto p = cdn_->GetFilePath("data", fileIdx[0] + ".index");
        fileIndex_ = std::make_shared<IndexInstance>(p);
    }
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0
                  ).count();
        std::cout << "File index loaded in " << std::ceil(ms) << "ms\n";
    }

    // --- Encoding ---
    auto encSize = std::stoull(buildConfig_->GetValues().at("encoding-size")[0]);
    t0 = std::chrono::steady_clock::now();
    auto encPath = cdn_->GetDecodedFilePath(
                       "data",
                       buildConfig_->GetValues().at("encoding")[1],
                       std::stoull(buildConfig_->GetValues().at("encoding-size")[1]),
                       encSize
                   );
    encoding_ = std::make_shared<EncodingInstance>(encPath, static_cast<int>(encSize));
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0
                  ).count();
        std::cout << "Encoding loaded in " << std::ceil(ms) << "ms\n";
    }

    // --- Root ---
    t0 = std::chrono::steady_clock::now();
    auto itRoot = buildConfig_->GetValues().find("root");
    if (itRoot == buildConfig_->GetValues().end())
        throw std::runtime_error("No root key found in build config");

    auto rootBytes = hexToBytes(itRoot->second[0]);
    auto rootKeys   = encoding_->FindContentKey(rootBytes);
    if (!rootKeys)  // assumes implicit operator!()
        throw std::runtime_error("Root key not found in encoding");

    auto rootHex = ToHexStringLower(rootKeys[0]);
    root_ = std::make_shared<RootInstance>(
                cdn_->GetDecodedFilePath("data", rootHex, 0, rootKeys.DecodedFileSize),
                *settings_
            );
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0
                  ).count();
        std::cout << "Root loaded in " << std::ceil(ms) << "ms\n";
    }

    // --- Install ---
    t0 = std::chrono::steady_clock::now();
    auto itInst = buildConfig_->GetValues().find("install");
    if (itInst == buildConfig_->GetValues().end())
        throw std::runtime_error("No install key found in build config");

    auto instBytes = hexToBytes(itInst->second[0]);
    auto instKeys  = encoding_->FindContentKey(instBytes);
    if (!instKeys)
        throw std::runtime_error("Install key not found in encoding");

    auto instHex = ToHexStringLower(instKeys[0]);
    install_ = std::make_shared<InstallInstance>(
                   cdn_->GetDecodedFilePath("data", instHex, 0, instKeys.DecodedFileSize)
               );
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0
                  ).count();
        std::cout << "Install loaded in " << std::ceil(ms) << "ms\n";
    }
}

std::vector<uint8_t> BuildInstance::OpenFileByFDID(uint32_t fileDataID)
{
    if (!root_)
        throw std::runtime_error("Root not loaded");

    auto entries = root_->GetEntriesByFDID(fileDataID);
    if (entries.empty())
        throw std::runtime_error("File not found in root");

    return OpenFileByCKey(entries[0].md5);
}

std::vector<uint8_t> BuildInstance::OpenFileByCKey(const std::string& cKey)
{
    return OpenFileByCKey(hexToBytes(cKey));
}

std::vector<uint8_t> BuildInstance::OpenFileByCKey(const std::vector<uint8_t>& cKey)
{
    if (!encoding_)
        throw std::runtime_error("Encoding not loaded");

    auto encRes = encoding_->FindContentKey(cKey);
    if (!encRes)
        throw std::runtime_error("File not found in encoding");

    return OpenFileByEKey(encRes[0], encRes.DecodedFileSize);
}

std::vector<uint8_t> BuildInstance::OpenFileByEKey(const std::string& eKey,
                                                   uint64_t decodedSize)
{
    return OpenFileByEKey(hexToBytes(eKey), decodedSize);
}

std::vector<uint8_t> BuildInstance::OpenFileByEKey(const std::vector<uint8_t>& eKey,
                                                   uint64_t decodedSize)
{
    if (!groupIndex_ || !fileIndex_)
        throw std::runtime_error("Indexes not loaded");

    auto [offset, size, archiveIdx] = groupIndex_->GetIndexInfo(eKey);
    std::vector<uint8_t> data;

    if (offset == -1) {
        auto fe = fileIndex_->GetIndexInfo(eKey);
        if (fe.size == -1) {
            std::cout << "Warning: EKey " << ToHexStringLower(eKey)
                      << " not found in group or file index and might not be available on CDN.\n";
            data = cdn_->GetFile("data",
                                 ToHexStringLower(eKey),
                                 0,
                                 decodedSize,
                                 true);
        }
        else {
            data = cdn_->GetFile("data",
                                 ToHexStringLower(eKey),
                                 fe.size,
                                 decodedSize,
                                 true);
        }
    }
    else {
        data = cdn_->GetFileFromArchive(ToHexStringLower(eKey),
                                        cdnConfig_->GetValues().at("archives")[archiveIdx],
                                        offset,
                                        size,
                                        decodedSize,
                                        true);
    }

    return data;
}
