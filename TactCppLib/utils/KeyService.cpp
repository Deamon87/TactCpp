#include "KeyService.h"

#include "KeyService.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

// Static member definitions
std::unordered_map<uint64_t, std::vector<uint8_t>> KeyService::keys_;
bool KeyService::initialized_ = KeyService::Initialize();

bool KeyService::Initialize() {
    // Perform one-time load of keys
    KeyService::LoadKeys();
    return true;
}

bool KeyService::TryGetKey(uint64_t keyName, std::vector<uint8_t>& outKey) {
    auto it = KeyService::keys_.find(keyName);
    if (it == KeyService::keys_.end())
        return false;
    outKey = it->second;
    return true;
}

void KeyService::SetKey(uint64_t keyName, const std::vector<uint8_t>& key) {
    KeyService::keys_[keyName] = key;
}

void KeyService::LoadKeys() {
    constexpr auto path = "WoW.txt";
    if (!std::filesystem::exists(path))
        return;

    std::ifstream infile(path);
    std::string line;
    while (std::getline(infile, line)) {
        std::istringstream iss(line);
        std::string hexName, hexKey;
        if (!(iss >> hexName >> hexKey))
            continue;

        // Parse key name as hex
        uint64_t lookup = 0;
        {
            std::istringstream hexiss(hexName);
            hexiss >> std::hex >> lookup;
        }

        // Convert hexKey string into bytes
        std::vector<uint8_t> key;
        key.reserve(hexKey.size() / 2);
        for (std::size_t i = 0; i + 1 < hexKey.size(); i += 2) {
            uint8_t byte = static_cast<uint8_t>(std::stoi(hexKey.substr(i, 2), nullptr, 16));
            key.push_back(byte);
        }

        KeyService::keys_.emplace(lookup, std::move(key));
    }
}
