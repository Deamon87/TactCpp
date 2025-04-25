#ifndef BINARYUTILS_H
#define BINARYUTILS_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

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
