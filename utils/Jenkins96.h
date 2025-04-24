//
// Created by Deamon on 4/22/2025.
//

#ifndef JENKINS96_H
#define JENKINS96_H

#include <cstdint>
#include <string>
#include <vector>

class Jenkins96 {
public:
    // Computes the 96‑bit Jenkins lookup3 hash, returns low 64 bits (c<<32 | b)
    static uint64_t ComputeHash(const std::string& str, bool fix = true);

private:
    ~Jenkins96() = delete;

    static uint64_t HashCore(std::vector<uint8_t>& data);

    static inline uint32_t Rot(uint32_t x, int k) {
        return (x << k) | (x >> (32 - k));
    }
};


#endif //JENKINS96_H
