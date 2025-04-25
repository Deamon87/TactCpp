#ifndef BSWAP_H
#define BSWAP_H

#include <stdint.h>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_byteswap_ulong)
#pragma intrinsic(_byteswap_ushort)

static inline uint32_t bswap32(uint32_t x) {
    return _byteswap_ulong(x);
}

static inline uint16_t bswap16(uint16_t x) {
    return _byteswap_ushort(x);
}

#elif defined(__clang__) || defined(__GNUC__)
static inline uint32_t bswap32(uint32_t x) {
    return __builtin_bswap32(x);
}

static inline uint16_t bswap16(uint16_t x) {
    return __builtin_bswap16(x);
}

#else
/* fallback: portable bit-twiddling */
static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0x000000FFu) << 24) |
        ((x & 0x0000FF00u) << 8) |
        ((x & 0x00FF0000u) >> 8) |
        ((x & 0xFF000000u) >> 24);
}

static inline uint16_t bswap16(uint16_t x) {
    return (uint16_t)(((x & 0x00FFu) << 8) |
        ((x & 0xFF00u) >> 8));
}
#endif

#endif /* BSWAP_H */