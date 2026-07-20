#include "KeyService.h"

#include "KeyService.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

#include "stringUtils.h"

// Static member definitions
std::unordered_map<uint64_t, std::array<uint8_t, 16>> KeyService::keys_;
std::unordered_map<std::string, std::array<uint8_t, 16>> KeyService::armadilloKeys_;

bool KeyService::initialized_ = KeyService::Initialize();

bool KeyService::Initialize() {
    // Perform one-time load of keys
    KeyService::LoadKeys();
    KeyService::LoadArmadilloKeys();
    return true;
}

bool KeyService::TryGetKey(uint64_t keyName, std::array<uint8_t, 16>& outKey) {
    auto it = KeyService::keys_.find(keyName);
    if (it == KeyService::keys_.end())
        return false;
    outKey = it->second;
    return true;
}

bool KeyService::TryGetArmadilloKey(const std::string &keyName, std::array<uint8_t, 16>& outKey) {
    auto it = KeyService::armadilloKeys_.find(keyName);
    if (it == KeyService::armadilloKeys_.end())
        return false;
    outKey = it->second;
    return true;
}

void KeyService::LoadKeys() {
    constexpr auto path = "WoW.txt";
    if (!std::filesystem::exists(path))
        return;

    KeyService::keys_.clear();

    std::ifstream infile(path);
    std::string line;
    while (std::getline(infile, line)) {
        std::istringstream iss(line);
        std::string hexName, hexKey;
        if (!(iss >> hexName >> hexKey))
            continue;

        if (hexKey.length() < 32) continue;

        // Parse key name as hex
        uint64_t lookup = 0;
        {
            std::istringstream hexiss(hexName);
            hexiss >> std::hex >> lookup;
        }

        // Convert hexKey string into bytes
        std::vector<uint8_t> v_key = TACTLibUtils::hexToBytes(hexKey);
        if (v_key.size() != 16) continue;

        std::array<uint8_t, 16> key;
        std::copy(v_key.begin(), v_key.end(), key.begin());

        KeyService::keys_.emplace(lookup, std::move(key));
    }
}

void KeyService::LoadArmadilloKeys() {
    KeyService::armadilloKeys_.clear();
        try {
        // Iterate through all entries in the current directory
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".ak") {

                // Open file in binary mode
                std::ifstream file(entry.path(), std::ios::binary);
                if (!file) {
//                    std::cerr << "Failed to open file: " << entry.path() << "\n";
                    continue;
                }

                // Read first 16 bytes
                std::array<uint8_t, 16> key;
                file.read((char *)(key.data()), 16);

                std::string filename_no_ext = entry.path().stem().string();
                KeyService::armadilloKeys_.emplace(filename_no_ext, key);
            }
        }
    } catch (const std::filesystem::filesystem_error& ex) {
//        std::cerr << "Filesystem error: " << ex.what() << "\n";
    }
}

