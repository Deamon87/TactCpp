#ifndef BINARYUTILS_H
#define BINARYUTILS_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

inline int16_t ReadInt16BE(const uint8_t* ptr) {
    return static_cast<int16_t>((ptr[0] << 8) | ptr[1]);
}

// Read big-endian unsigned 16-bit
inline uint16_t ReadUInt16BE(const uint8_t* ptr) {
    return (uint16_t(ptr[0]) << 8) | uint16_t(ptr[1]);
}

// Read big-endian signed 32-bit
inline int32_t ReadInt32BE(const uint8_t* ptr) {
    return  (int32_t(ptr[0]) << 24) |
           (int32_t(ptr[1]) << 16) |
           (int32_t(ptr[2]) <<  8) |
            int32_t(ptr[3]);
}

inline uint32_t ReadUInt32BE(const uint8_t* ptr) {
    return  (uint32_t(ptr[0]) << 24) |
            (uint32_t(ptr[1]) << 16) |
            (uint32_t(ptr[2]) <<  8) |
            uint32_t(ptr[3]);
}


inline uint32_t ReadInt32LE(const uint8_t* ptr) {
    return uint32_t(ptr[0])       |
           (uint32_t(ptr[1]) << 8)  |
           (uint32_t(ptr[2]) << 16) |
           (uint32_t(ptr[3]) << 24);
}

// Read big-endian unsigned 40-bit
inline uint64_t ReadUInt40BE(const uint8_t* ptr) {

    return  (uint64_t(ptr[0]) << 32) |
            (uint64_t(ptr[1]) << 24) |
            (uint64_t(ptr[2]) << 16) |
            (uint64_t(ptr[3]) <<  8) |
             uint64_t(ptr[4]);
}

// Read a NUL-terminated string from ptr, returns length (without NUL)
inline std::string ReadNullTermString(
    const uint8_t* buf, size_t bufLen, size_t offset)
{
    if (offset >= bufLen)
        throw std::runtime_error("String offset out of range");
    const char* start = reinterpret_cast<const char*>(buf + offset);
    size_t maxLen = bufLen - offset;
#ifdef _MSC_VER
    size_t len = strnlen_s(start, maxLen);
#else
    size_t len = strnlen(start, maxLen);
#endif
    return std::string(start, len);
}

// Lexicographical compare two byte sequences
inline bool SequenceEqual(const uint8_t* a, const uint8_t* b, size_t len) {
    return std::memcmp(a, b, len) == 0;
}

// Iterator that steps through entries by fixed stride
struct EntryIterator {
    using iterator_category = std::random_access_iterator_tag;
    using value_type = const uint8_t*;
    using difference_type = std::ptrdiff_t;
    using pointer = const uint8_t**;
    using reference = const uint8_t*;

    const uint8_t* ptr;
    size_t stride;

    EntryIterator(const uint8_t* p, size_t s) : ptr(p), stride(s) {}
    reference operator*() const { return ptr; }
    EntryIterator& operator++() { ptr += stride; return *this; }
    EntryIterator& operator--() { ptr -= stride; return *this; }
    EntryIterator operator++(int) { auto tmp = *this; ptr += stride; return tmp; }
    EntryIterator& operator+=(difference_type n) { ptr += stride * n; return *this; }
    EntryIterator operator+(difference_type n) const { return EntryIterator(ptr + stride * n, stride); }
    EntryIterator operator-(difference_type n) const { return EntryIterator(ptr - stride * n, stride); }
    difference_type operator-(const EntryIterator& other) const { return (ptr - other.ptr) / (difference_type)stride; }
    bool operator<(const EntryIterator& other) const { return ptr < other.ptr; }
    bool operator==(const EntryIterator& other) const { return ptr == other.ptr; }
    bool operator!=(const EntryIterator& other) const { return !(*this == other); }
};

#endif //BINARYUTILS_H
