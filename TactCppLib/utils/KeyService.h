#ifndef KEYSERVICE_H
#define KEYSERVICE_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string>

class KeyService {
public:
    // Try to get a key; returns true if found
    static bool TryGetKey(uint64_t keyName, std::array<uint8_t, 16>& outKey);
    static bool TryGetArmadilloKey(const std::string &keyName, std::array<uint8_t, 16>& outKey);

    // (Re)load keys from disk
    static void LoadKeys();
    static void LoadArmadilloKeys();

private:
    KeyService() = delete;  // no instances

    // where we store the keys
    static std::unordered_map<uint64_t, std::array<uint8_t, 16>> keys_;
    static std::unordered_map<std::string, std::array<uint8_t, 16>> armadilloKeys_;

    // helper to do one‑time init
    static bool initialized_;
    static bool Initialize();
};




#endif //KEYSERVICE_H
