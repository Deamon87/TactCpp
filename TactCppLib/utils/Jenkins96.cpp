//
// Created by Deamon on 4/22/2025.
//

#include "Jenkins96.h"

#include "Jenkins96.h"
#include <algorithm>
#include <cctype>

uint64_t Jenkins96::ComputeHash(const std::string& str, bool fix) {
    std::string temp = str;
    if (fix) {
        for (char& ch : temp) {
            if (ch == '/') ch = '\\';
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
    }

    std::vector<uint8_t> data(temp.begin(), temp.end());
    uint64_t hashValue = HashCore(data);
    return hashValue;
}

uint64_t Jenkins96::HashCore(std::vector<uint8_t>& data) {
    uint32_t length = static_cast<uint32_t>(data.size());
    uint32_t a = 0xdeadbeef + length;
    uint32_t b = a;
    uint32_t c = a;

    if (length == 0) {
        uint64_t hashValue = (static_cast<uint64_t>(c) << 32) | b;
        return hashValue;
    }

    size_t newLen = length + ((12 - (length % 12)) % 12);
    data.resize(newLen, 0);

    const uint8_t* bytes = data.data();
    size_t len = data.size();

    // Process 12-byte blocks
    for (size_t j = 0; j + 12 <= len; j += 12) {
        const uint32_t* p = reinterpret_cast<const uint32_t*>(bytes + j);
        a += p[0];
        b += p[1];
        c += p[2];

        a -= c;  a ^= Rot(c, 4);  c += b;
        b -= a;  b ^= Rot(a, 6);  a += c;
        c -= b;  c ^= Rot(b, 8);  b += a;
        a -= c;  a ^= Rot(c, 16); c += b;
        b -= a;  b ^= Rot(a, 19); a += c;
        c -= b;  c ^= Rot(b, 4);  b += a;
    }

    // Final mix of last 12 bytes
    size_t i = len - 12;
    const uint32_t* p = reinterpret_cast<const uint32_t*>(bytes + i);
    a += p[0];
    b += p[1];
    c += p[2];

    c ^= b; c -= Rot(b, 14);
    a ^= c; a -= Rot(c, 11);
    b ^= a; b -= Rot(a, 25);
    c ^= b; c -= Rot(b, 16);
    a ^= c; a -= Rot(c, 4);
    b ^= a; b -= Rot(a, 14);
    c ^= b; c -= Rot(b, 24);

    uint64_t hashValue = (static_cast<uint64_t>(c) << 32) | b;
    return hashValue;
}
