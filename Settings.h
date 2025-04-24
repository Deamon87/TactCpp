//
// Created by Deamon on 4/23/2025.
//

#ifndef SETTINGS_H
#define SETTINGS_H

#include <optional>
#include <filesystem>

#include "RootInstance.h"

struct Settings {
    std::string Region        = "us";
    std::string Product       = "wow";
    RootInstance::LocaleFlags  Locale       = RootInstance::LocaleFlags::enUS;
    RootInstance::LoadMode     RootMode     = RootInstance::LoadMode::Normal;
    std::optional<std::filesystem::path> BaseDir;
    std::optional<std::string> BuildConfig;
    std::optional<std::string> CDNConfig;
    std::filesystem::path CacheDir = "cache";
    bool        ListfileFallback = true;
    std::string ListfileURL   = "https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv";
};

#endif //SETTINGS_H
