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
#include "ListFile.h"

class BuildInstance {
public:
    explicit BuildInstance(const Settings &settings_);
    ~BuildInstance();

    // file‐opening helpers
    std::vector<uint8_t> OpenFileByName(const std::string &fileName);
    std::vector<uint8_t> OpenFileByFDID(uint32_t fileDataID);
    std::vector<uint8_t> OpenFileByCKey(const std::string& cKey);
    std::vector<uint8_t> OpenFileByCKey(const std::vector<uint8_t>& cKey);
    std::vector<uint8_t> OpenFileByEKey(const std::string& eKey,
                                        uint64_t decodedSize = 0);
    std::vector<uint8_t> OpenFileByEKey(const std::vector<uint8_t>& eKey,
                                        uint64_t decodedSize = 0);

    // getters
    const std::unique_ptr<TactConfig>             &GetBuildConfig() const { return buildConfig_; }
    const std::unique_ptr<TactConfig>             &GetCDNConfig()   const { return cdnConfig_;   }
    const std::unique_ptr<TACTSharp::EncodingInstance>   &GetEncoding()    const { return encoding_;    }
    const std::unique_ptr<RootInstance>       &GetRoot()        const { return root_;        }
    const std::unique_ptr<InstallInstance>    &GetInstall()     const { return install_;     }
    const std::unique_ptr<IndexInstance>      &GetGroupIndex()  const { return groupIndex_;  }
    const std::unique_ptr<IndexInstance>      &GetFileIndex()   const { return fileIndex_;   }
    const std::unique_ptr<CDN>                &GetCDN()         const { return cdn_;         }
    const std::unique_ptr<ListFile>           &GetListFile()    const { return listFile_;   }

    // populate indexes, encoding, root & install instances
    void Load();
private:
    Settings settings_;

    std::unique_ptr<TactConfig>           buildConfig_ = nullptr;
    std::unique_ptr<TactConfig>           cdnConfig_= nullptr;
    std::unique_ptr<TACTSharp::EncodingInstance> encoding_= nullptr;
    std::unique_ptr<RootInstance>     root_= nullptr;
    std::unique_ptr<InstallInstance>  install_= nullptr;
    std::unique_ptr<IndexInstance>    groupIndex_= nullptr;
    std::unique_ptr<IndexInstance>    fileIndex_= nullptr;
    std::unique_ptr<CDN>              cdn_= nullptr;
    std::unique_ptr<ListFile>         listFile_= nullptr;

    // load the build+CDN configs (path or 32-char hex ID)
    void LoadConfigs();
    void LoadVersionInfo();
};



#endif //BUILDINSTANCE_H
