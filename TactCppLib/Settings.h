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
    RootWoW::LocaleFlags  Locale       = RootWoW::LocaleFlags::enUS;
    RootWoW::LoadMode     RootMode     = RootWoW::LoadMode::Normal;
    std::optional<std::filesystem::path> BaseDir;
    std::string BuildConfigPathOrHash;
    std::string CDNConfigPathOrHash;

    std::string BuildConfig;
    std::string CDNConfig;

    std::filesystem::path CacheDir = "cache";
    bool        ListfileFallback = true;
    std::string ListfileURL   = "https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv";
};

#endif //SETTINGS_H
