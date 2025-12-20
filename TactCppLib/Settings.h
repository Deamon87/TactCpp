//
// Created by Deamon on 4/23/2025.
//

#ifndef SETTINGS_H
#define SETTINGS_H

#include <optional>
#include <filesystem>

#include "wow/WoWRootFlags.h"

struct Settings {
    std::string Region        = "us";
    std::string Product       = "wow";
    std::optional<std::string> TactName;
    RootWoW::LocaleFlags  Locale       = RootWoW::LocaleFlags::enUS;
    RootWoW::LoadMode     RootMode     = RootWoW::LoadMode::Normal;
    std::optional<std::filesystem::path> BaseDir;
    std::string BuildConfigPathOrHash;
    std::string CDNConfigPathOrHash;
    std::string ArmadilloKey = "";

    std::string BuildConfig;
    std::string CDNConfig;
    std::string ProductConfigHash;

    bool allowOnlineDownload = false;

    bool preferLowViolence = false;
    bool preferHiResTextures = true;

    std::filesystem::path CacheDir = "cache";
    std::string ListfileURL   = "https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv";
    std::optional<std::filesystem::path> ListfilePath;
    bool loadListFile = false;

    bool useTactLocal = false;
    std::optional<std::filesystem::path> LocalTactPath;
};

#endif //SETTINGS_H
