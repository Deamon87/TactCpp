#ifndef SALSA_DECRYPT_H
#define SALSA_DECRYPT_H

#include <vector>
#include <cstdint>
#include <string>

class SalsaDecrypt {
public:
    /**
     * Decrypts data using Salsa20 algorithm in-place.
     * 
     * @param data Input data to decrypt (will be decrypted in-place)
     * @param offset Starting byte offset in the encrypted stream (used to initialize counter)
     * @param ivHex IV as hex string (must be 16 hex characters = 8 bytes)
     * @param key Encryption key (must be exactly 16 bytes)
     */
    static void Decrypt(std::vector<uint8_t>& data, uint64_t offset, const std::string& ivHex, const std::array<uint8_t, 16>& key);

    /**
     * Decrypts data using Salsa20 algorithm in-place.
     *
     * @param data Input data to decrypt (will be decrypted in-place)
     * @param offset Starting byte offset in the encrypted stream (used to initialize counter)
     * @param iv IV as raw bytes (must be exactly 8 bytes)
     * @param key Encryption key (must be exactly 16 bytes)
     */
    static void Decrypt(std::vector<uint8_t>& data, uint64_t offset, const std::vector<uint8_t>& iv, const std::array<uint8_t, 16>& key);
};

#endif // SALSA_DECRYPT_H

