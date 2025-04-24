// tacttool.cpp

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <execution>
#include <ranges>

#include "../3rdparty/cxxopts.hpp"
#include "../TactCppLib/BuildInstance.h"
#include "../TactCppLib/BuildInfo.h"
#include "../TactCppLib/utils/Jenkins96.h"
#include "../TactCppLib/utils/stringUtils.h"
#include "../TactCppLib/utils/TactConfigParser.h"

namespace fs = std::filesystem;
using namespace TACTSharp;

enum class InputMode { List, EKey, CKey, FDID, FileName };

struct ExtractionTarget {
    std::vector<uint8_t> eKey;
    uint64_t decodedSize;
    std::string fileName;
};

static std::optional<InputMode> Mode;
static std::string Input, Output;
static std::vector<ExtractionTarget> extractionTargets;
static std::mutex extractionMutex;
static BuildInstance build;

static std::string toHexLower(const std::vector<uint8_t>& data) {
    static constexpr char tbl[] = "0123456789abcdef";
    std::string s; s.reserve(data.size()*2);
    for (auto b : data) {
        s += tbl[b>>4];
        s += tbl[b&0xF];
    }
    return s;
}

void HandleEKey(const std::string& eKeyHex, const std::optional<std::string>& filename) {
    if (eKeyHex.size()!=32 ||
        !std::all_of(eKeyHex.begin(), eKeyHex.end(), [](char c){ return std::isxdigit(c)&&std::islower(c); }))
    {
        std::cout << "Skipping " << eKeyHex
                  << ", invalid formatting for EKey (expected 32-char hex).\n";
        return;
    }
    ExtractionTarget t{ hexToBytes(eKeyHex), 0, filename.value_or(eKeyHex) };
    std::lock_guard lk(extractionMutex);
    extractionTargets.push_back(std::move(t));
}

void HandleCKey(const std::string& cKeyHex, const std::optional<std::string>& filename) {
    if (cKeyHex.size()!=32 ||
        !std::all_of(cKeyHex.begin(), cKeyHex.end(), [](char c){ return std::isxdigit(c)&&std::islower(c); }))
    {
        std::cout << "Skipping " << cKeyHex
                  << ", invalid formatting for CKey (expected 32-char hex).\n";
        return;
    }
    auto cKeyBytes = hexToBytes(cKeyHex);
    auto fileKeys = build.GetEncoding()->FindContentKey(cKeyBytes);
    if (fileKeys.empty()) {
        std::cout << "Skipping " << cKeyHex << ", CKey not found in encoding.\n";
        return;
    }
    ExtractionTarget t{ fileKeys.key(0), fileKeys.decodedFileSize, filename.value_or(cKeyHex) };
    std::lock_guard lk(extractionMutex);
    extractionTargets.push_back(std::move(t));
}

void HandleFDID(const std::string& fdidStr, const std::optional<std::string>& filename) {
    uint32_t fdid=0;
    try { fdid = std::stoul(fdidStr); }
    catch(...) {
        std::cout << "Skipping FDID " << fdidStr
                  << ", invalid format (expected unsigned integer).\n";
        return;
    }
    auto entries = build.GetRoot()->GetEntriesByFDID(fdid);
    if (entries.empty()) {
        std::cout << "Skipping FDID " << fdidStr << ", not found in root.\n";
        return;
    }
    auto fileKeys = build.GetEncoding()->FindContentKey(entries[0].md5);
    if (fileKeys.empty()) {
        std::cout << "Skipping FDID " << fdidStr
                  << ", CKey not found in encoding.\n";
        return;
    }
    auto fileNameToSave = filename.value_or(fdidStr);
    fileNameToSave = fileNameToSave.empty() ? fdidStr : fileNameToSave;

    ExtractionTarget t{ fileKeys.key(0), fileKeys.decodedFileSize, fileNameToSave};
    std::lock_guard lk(extractionMutex);
    extractionTargets.push_back(std::move(t));
}

bool ichar_equals(char a, char b)
{
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
}
bool iequals(std::string_view lhs, std::string_view rhs)
{
    return std::ranges::equal(lhs, rhs, ichar_equals);
}

void HandleFileName(const std::string& fname, const std::optional<std::string>& outName) {
    // normalize separators
    auto normName = fname;
    std::replace(normName.begin(), normName.end(), '/', '\\');

    auto entries =
        build.GetInstall()->getEntries() |
        std::views::filter([&](auto& entry) {
            return iequals(entry.name, normName);
        }) |
        std::ranges::to<std::vector>();

    if (entries.empty()) {
        // fallback via Jenkins96 lookup or listfile...

        auto hash = Jenkins96::ComputeHash(normName, true);
        auto byLookup = build.GetRoot()->GetEntriesByLookup(hash);
        if (!byLookup.empty()) {
            HandleCKey(MD5ToHexLower(byLookup[0].md5), fname);
            return;
        }
        // if (build.GetSettings().ListfileFallback) {
        //     std::cout << "No file by name \"" << fname
        //               << "\" in install; checking listfile.\n";
        //     if (!listfile.Initialized())
        //         listfile.Initialize(build.CDN(), build.Settings());
        //     auto id = listfile.GetFDID(fname);
        //     if (id==0) {
        //         std::cout << "No file by name \"" << fname
        //                   << "\" found in listfile. Skipping..\n";
        //         return;
        //     }
        //     HandleFDID(std::to_string(id), fname);
        //     return;
        // }
        std::cout << "No file by name \"" << fname
                  << "\" found and fallback disabled. Skipping..\n";
        return;
    }

    std::vector<uint8_t> targetMd5 = entries[0].md5;
    if (entries.size() > 1) {
        auto usEntries =
            std::find_if(entries.begin(), entries.end(), [](auto &e) {
                return !(e.tags | std::views::filter([](const auto &tag) { return tag == "4=US";})).empty();
            });
        if (usEntries!=entries.end()) {
            std::cout << "Multiple results for " << fname << ", using US version..\n";
            targetMd5 = usEntries->md5;
        } else {
            std::cout << "Multiple results for " << fname<< ", using first result..\n";
        }
    }

    auto fileKeys = build.GetEncoding()->FindContentKey(targetMd5);
    if (fileKeys.empty())
        throw std::runtime_error("EKey not found in encoding");

    ExtractionTarget t{ fileKeys.key(0), fileKeys.decodedFileSize,
                       outName.value_or(fname) };
    std::lock_guard lk(extractionMutex);
    extractionTargets.push_back(std::move(t));
}

void HandleList(const std::string& listPath) {
    std::ifstream ifs(listPath);
    if (!ifs) {
        std::cout << "Input file list " << listPath
                  << " not found, skipping extraction..\n";
        return;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        auto parts = line | std::views::split(';')
                     | std::ranges::views::transform([](auto rng){
                         return std::string_view(&*rng.begin(), std::ranges::distance(rng));
                     });
        std::vector<std::string> ps(parts.begin(), parts.end());
        if (ps.empty()) continue;
        if (ps[0]=="ckey" || ps[0]=="chash")        HandleCKey(ps[1], ps.size()>2?std::optional(ps[2]):std::nullopt);
        else if (ps[0]=="ekey" || ps[0]=="ehash")   HandleEKey(ps[1], ps.size()>2?std::optional(ps[2]):std::nullopt);
        else if (ps[0]=="install")                  HandleFileName(ps[1], ps.size()>2?std::optional(ps[2]):std::nullopt);
        else if (std::all_of(ps[0].begin(), ps[0].end(), ::isdigit))
                                                    HandleFDID(ps[0], ps.size()>1?std::optional(ps[1]):std::nullopt);
        else                                       HandleFileName(ps[0], ps.size()>1?std::optional(ps[1]):std::nullopt);
    }
}

int main(int argc, char* argv[]) {
    try {
        cxxopts::Options opts("TACTTool", "Extraction tool using the TACTSharp library");
        opts.add_options()
          ("b,buildconfig", "Build config (hex or file)", cxxopts::value<std::string>())
          ("c,cdnconfig"  , "CDN config (hex or file)", cxxopts::value<std::string>())
          ("p,product"    , "TACT product", cxxopts::value<std::string>()->default_value("wow"))
          ("r,region"     , "Region",        cxxopts::value<std::string>()->default_value("us"))
          ("l,locale"     , "Locale",        cxxopts::value<std::string>()->default_value("enUS"))
          ("m,mode"       , "Input mode",    cxxopts::value<std::string>())
          ("i,inputvalue" , "Input value",   cxxopts::value<std::string>())
          ("o,output"     , "Output path",   cxxopts::value<std::string>())
          ("d,basedir"    , "Base install dir", cxxopts::value<std::string>())
          ("h,help", "Print help");
        auto res = opts.parse(argc, argv);
        if (res.count("help")) {
            std::cout << opts.help() << "\n";
            return 0;
        }

        if (res.count("buildconfig"))build.GetSettings()->BuildConfig = res["buildconfig"].as<std::string>();
        if (res.count("cdnconfig"))  build.GetSettings()->CDNConfig   = res["cdnconfig"].as<std::string>();
        if (res.count("product"))    build.GetSettings()->Product     = res["product"].as<std::string>();
        if (res.count("region"))     build.GetSettings()->Region      = res["region"].as<std::string>();
        if (res.count("locale"))     build.GetSettings()->Locale      = RootInstance::StringToLocaleFlag.at(res["locale"].as<std::string>());
        if (res.count("inputvalue")) Input  = res["inputvalue"].as<std::string>();
        if (res.count("output"))     Output = res["output"].as<std::string>();

        if (res.count("mode")) {
            auto m = res["mode"].as<std::string>();
            std::transform(m.begin(), m.end(), m.begin(), ::tolower);
            if      (m=="list")     Mode = InputMode::List;
            else if (m=="ekey"||m=="ehash") Mode = InputMode::EKey;
            else if (m=="ckey"||m=="chash") Mode = InputMode::CKey;
            else if (m=="id"||m=="fdid")    Mode = InputMode::FDID;
            else if (m=="install"||m=="name"||m=="filename")
                                       Mode = InputMode::FileName;
            else throw std::runtime_error("Invalid input mode");
        } else {
            throw std::runtime_error("No input mode specified");
        }

        if (*Mode == InputMode::List && Output.empty())
            Output = "extract";

        // Load configs – from basedir or patch service
        if (res.count("basedir")) {
            build.GetSettings()->BaseDir = res["basedir"].as<std::string>();

            fs::path bp = fs::path(build.GetSettings()->BaseDir.value()) / ".build.info";
            if (!fs::exists(bp)) throw std::runtime_error("No .build.info in basedir");
            BuildInfo bi(bp.string(), *build.GetSettings(), *build.GetCDN());


            auto matchedEntries = bi.Entries | std::views::filter([](const auto &rng) {
                return rng.Product == build.GetSettings()->Product;
            }) | std::views::take(1) | std::ranges::to<std::vector>();

            if (matchedEntries.empty())
                throw std::runtime_error("No build found for product " + build.GetSettings()->Product + " in .build.info, are you sure this product is installed?");

            auto const &buildInfoEntry = matchedEntries[0];
            build.GetSettings()->BuildConfig = buildInfoEntry.BuildConfig;
            build.GetSettings()->CDNConfig = buildInfoEntry.CDNConfig;
            build.GetCDN()->setProductDirectory(buildInfoEntry.CDNPath);

        } else {
            auto versions = build.GetCDN()->GetPatchServiceFile(build.GetSettings()->Product, "versions");
            TactConfigParser::parse(versions, {"Region", "BuildConfig", "CDNConfig"}, [&](const auto &rec) {
                if (build.GetSettings()->Region != rec.at("Region")) { return true;} // continue if region do no match

                build.GetSettings()->BuildConfig = rec.at("BuildConfig");
                build.GetSettings()->CDNConfig = rec.at("CDNConfig");

                return false;
            });
        }

        if (build.GetSettings()->BuildConfig.value_or("").empty() || build.GetSettings()->CDNConfig.value_or("").empty()) {
            std::cerr << "Missing build or CDN config, exiting..\n";
            return 1;
        }

        build.LoadConfigs(build.GetSettings()->BuildConfig.value(), build.GetSettings()->CDNConfig.value());

        // Load
        auto t0 = std::chrono::high_resolution_clock::now();
        build.Load();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "Build " << build.GetBuildConfig()->Values.at("build-name")[0]
                  << " loaded in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count()
                  << "ms\n";

        if (Input.empty()) {
            std::cerr << "No input given, skipping extraction..\n";
            return 1;
        }

        // Handle inputs
        switch (*Mode) {
        case InputMode::List:     HandleList(Input);                 break;
        case InputMode::EKey:     HandleEKey(Input, Output);        break;
        case InputMode::CKey:     HandleCKey(Input, Output);        break;
        case InputMode::FDID:     HandleFDID(Input, Output);        break;
        case InputMode::FileName: HandleFileName(Input, Output);    break;
        }

        if (extractionTargets.empty()) {
            std::cerr << "No files to extract, exiting..\n";
            return 1;
        }

        std::cout << "Extracting " << extractionTargets.size()
                  << " file" << (extractionTargets.size()>1?"s":"") << "..\n";

        // Parallel extract
        std::for_each(std::execution::par, extractionTargets.begin(), extractionTargets.end(),
            [&](auto &t){
                auto hex = toHexLower(t.eKey);
                std::cout << "Extracting " << hex
                          << " to " << t.fileName << std::endl;;
                try {
                    auto data = build.OpenFileByEKey(t.eKey, t.decodedSize);
                    fs::path out = t.fileName;
                    if (!out.parent_path().empty()) {
                        fs::create_directories(out.parent_path());
                    }
                    std::ofstream ofs(out, std::ios::binary);
                    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
                } catch (std::exception& e) {
                    std::cerr << "Failed to extract " << t.fileName
                              << " (" << hex << "): " << e.what() << "\n";
                }
            }
        );

        auto t2 = std::chrono::high_resolution_clock::now();
        std::cout << "Total time: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t2-t0).count()
                  << "ms\n";
    }
    catch (std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}