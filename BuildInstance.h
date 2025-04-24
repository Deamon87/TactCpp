//
// Created by Deamon on 4/23/2025.
//

#ifndef BUILDINSTANCE_H
#define BUILDINSTANCE_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "Config.h"
#include "EncodingInstance.h"
#include "RootInstance.h"
#include "InstallInstance.h"
#include "IndexInstance.h"
#include "CDN.h"
#include "Settings.h"

class BuildInstance {
public:
    BuildInstance();

    // load the build+CDN configs (path or 32-char hex ID)
    void LoadConfigs(const std::string& buildConfigPath,
                     const std::string& cdnConfigPath);

    // populate indexes, encoding, root & install instances
    void Load();

    // file‐opening helpers
    std::vector<uint8_t> OpenFileByFDID(uint32_t fileDataID);
    std::vector<uint8_t> OpenFileByCKey(const std::string& cKey);
    std::vector<uint8_t> OpenFileByCKey(const std::vector<uint8_t>& cKey);
    std::vector<uint8_t> OpenFileByEKey(const std::string& eKey,
                                        uint64_t decodedSize = 0);
    std::vector<uint8_t> OpenFileByEKey(const std::vector<uint8_t>& eKey,
                                        uint64_t decodedSize = 0);

    // getters
    std::shared_ptr<Config>             GetBuildConfig() const { return buildConfig_; }
    std::shared_ptr<Config>             GetCDNConfig()   const { return cdnConfig_;   }
    std::shared_ptr<TACTSharp::EncodingInstance>   GetEncoding()    const { return encoding_;    }
    std::shared_ptr<RootInstance>       GetRoot()        const { return root_;        }
    std::shared_ptr<InstallInstance>    GetInstall()     const { return install_;     }
    std::shared_ptr<IndexInstance>      GetGroupIndex()  const { return groupIndex_;  }
    std::shared_ptr<IndexInstance>      GetFileIndex()   const { return fileIndex_;   }
    std::shared_ptr<CDN>                GetCDN()         const { return cdn_;         }
    std::shared_ptr<Settings>           GetSettings()    const { return settings_;    }

private:
    std::shared_ptr<Config>           buildConfig_;
    std::shared_ptr<Config>           cdnConfig_;
    std::shared_ptr<TACTSharp::EncodingInstance> encoding_;
    std::shared_ptr<RootInstance>     root_;
    std::shared_ptr<InstallInstance>  install_;
    std::shared_ptr<IndexInstance>    groupIndex_;
    std::shared_ptr<IndexInstance>    fileIndex_;
    std::shared_ptr<CDN>              cdn_;
    std::shared_ptr<Settings>         settings_;
};



#endif //BUILDINSTANCE_H
