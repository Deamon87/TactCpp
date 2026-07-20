#include "SalsaDecrypt.h"
#include "stringUtils.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>

inline uint32_t rotl32(uint32_t x, unsigned int n) {
    return (x << n) | (x >> (32 - n));
}

inline void qr(uint32_t& x0, uint32_t& x1, uint32_t& x2, uint32_t& x3) {
   x1 ^= rotl32(x0 + x3, 7);
   x2 ^= rotl32(x1 + x0, 9);
   x3 ^= rotl32(x2 + x1, 13);
   x0 ^= rotl32(x3 + x2, 18);
}

inline void salsa20_round(std::array<uint32_t, 16>& x) {
    // Column quarter-rounds
    qr(x[0],  x[4],  x[8],  x[12]);
    qr(x[5],  x[9],  x[13], x[1]);
    qr(x[10], x[14], x[2],  x[6]);
    qr(x[15], x[3],  x[7],  x[11]);

    // Row quarter-rounds (diagonals)
    qr(x[0],  x[1],  x[2],  x[3]);
    qr(x[5],  x[6],  x[7],  x[4]);
    qr(x[10], x[11], x[8],  x[9]);
    qr(x[15], x[12], x[13], x[14]);
}


void salsa20Math(const std::array<uint32_t, 16>& initialDancers, std::array<uint32_t, 16>& output) {

    std::array<uint32_t, 16> x = initialDancers;

    for(uint32_t i = 0; i < 10; i++) {
        salsa20_round(x);
    }

    for(uint32_t i = 0; i < 16; i++)
        output[i] = (initialDancers[i] + x[i]);
}


void SalsaDecrypt::Decrypt(std::vector<uint8_t>& data, uint64_t offset, const std::string& ivHex, const std::array<uint8_t, 16>& key) {
    // Parse IV from hex string (must be 8 bytes = 16 hex characters)
    Decrypt(data, offset, TACTLibUtils::hexToBytes(ivHex), key);
}

void SalsaDecrypt::Decrypt(std::vector<uint8_t>& data, uint64_t offset, const std::vector<uint8_t>& iv, const std::array<uint8_t, 16>& key) {
    // Validate key size (must be 16 bytes)
    if (key.size() != 16) {
        throw std::invalid_argument("Key must be exactly 16 bytes");
    }

    // Validate IV size (must be 8 bytes)
    if (iv.size() != 8) {
        throw std::invalid_argument("IV must be exactly 8 bytes");
    }
    uint32_t IV[2] = {0, 0};
    std::copy(iv.begin(), iv.end(), (uint8_t *) &IV[0]);

    uint32_t xLow = *(uint32_t*) key.data();
    uint32_t xHi =  *(uint32_t*) (key.data() + 4);

    uint32_t yLow = *(uint32_t*) (key.data() + 8);
    uint32_t yHi =  *(uint32_t*) (key.data() + 12);

    std::array<uint32_t, 16> state;
    state[0] = 1634760805;
    state[5] = 824206446;
    state[6] = IV[0];
    state[7] = IV[1];
    state[8] = offset / 64;
    state[9] = 0;
    state[10] = 2036477238;
    state[15] = 1797285236;

    // set the key for this iteration
    state[1] = state[11] = xLow;
    state[2] = state[12] = xHi;
    state[3] = state[13] = yLow;
    state[4] = state[14] = yHi;

    int decryptOffset = offset % 64;

    int j = 0;
    int      length  = data.size();
    uint8_t* dataPtr = data.data();
    while (length > 0) {
        std::array<uint32_t, 16> output;
        salsa20Math(state, output);

        // Cast output to byte array for direct byte access
        const uint8_t* keystream = (const uint8_t*)(output.data()) + decryptOffset;

        int blockSize = std::min(64 - decryptOffset, length);

        int remaining = blockSize;

        // Process 8-byte chunks
        while (remaining >= 8) {
            *(uint64_t*)(dataPtr) ^= *(const uint64_t*)(keystream);

            dataPtr += 8;
            keystream += 8;
            remaining -= 8;
        }
        
        // Process remaining bytes one by one
        while (remaining > 0) {
            *dataPtr++ ^= *keystream++;
            remaining--;
        }
        
        j += blockSize;
        length -= blockSize;
        decryptOffset = (decryptOffset + blockSize) % 64;

        state[8] += 1;
        if (state[8] == 0) {
            state[9] += 1;
        }
    }

}

