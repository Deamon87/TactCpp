//
// Created by Deamon on 4/22/2025.
//

#ifndef BLTE_H
#define BLTE_H

#include <cstdint>
#include <vector>
#include <cstddef>

class BLTE {
public:
    // Decode BLTE-encoded data. Throws on error.
    static std::vector<uint8_t> Decode(const std::vector<uint8_t>& data, uint64_t totalDecompSize = 0);

private:
    static void HandleDataBlock(char mode,
                                const uint8_t* compData, size_t compSize,
                                int chunkIndex,
                                uint8_t* decompData, size_t decompSize);
    static bool TryDecrypt(const uint8_t* data, size_t dataSize,
                           int chunkIndex,
                           std::vector<uint8_t>& output);
    static uint32_t ReadUInt32BE(const uint8_t* ptr);
    static uint32_t ReadUInt24BE(const uint8_t* ptr);
    static uint64_t ReadUInt64LE(const uint8_t* ptr);
};

#endif //BLTE_H
