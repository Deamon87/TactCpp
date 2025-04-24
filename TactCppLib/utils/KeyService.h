#ifndef KEYSERVICE_H
#define KEYSERVICE_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string>

class Salsa20;  // forward‑declare your Salsa20 implementation

class KeyService {
public:
    // Access to the singleton Salsa20 instance
    static Salsa20& SalsaInstance();

    // Try to get a key; returns true if found
    static bool TryGetKey(uint64_t keyName, std::vector<uint8_t>& outKey);

    // Insert or overwrite a key
    static void SetKey(uint64_t keyName, const std::vector<uint8_t>& key);

    // (Re)load keys from disk
    static void LoadKeys();

private:
    KeyService() = delete;  // no instances

    // where we store the keys
    static std::unordered_map<uint64_t, std::vector<uint8_t>> keys_;

    // helper to do one‑time init
    static bool initialized_;
    static bool Initialize();
};




#endif //KEYSERVICE_H
