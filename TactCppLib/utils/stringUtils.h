//
// Created by Deamon on 4/23/2025.
//

#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <string>
#include <stdexcept>
#include <cstdint>


inline bool startsWith(std::string_view str, std::string_view prefix)
{
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

inline std::vector<std::string> tokenize(std::string const &str, const std::string delim)
{
    size_t prev_start = 0;
    size_t start = 0;

    std::vector<std::string> out;
    while ((start = str.find(delim, start)) != std::string::npos)
    {
        out.push_back(str.substr(prev_start, start-prev_start));
        prev_start = ++start;
    }
    out.push_back(str.substr(prev_start, start-prev_start));

    return out;
}

inline std::vector<std::string> tokenizeAndFilter(std::string const &str, const std::string delim, std::function<bool(std::string&)> filterFunc)
{
    size_t prev_start = 0;
    size_t start = 0;

    std::vector<std::string> out;
    while ((start = str.find(delim, start)) != std::string::npos)
    {
        auto s = str.substr(prev_start, start-prev_start);
        if (filterFunc(s)) out.push_back(s);
        prev_start = ++start;
    }
    auto s = str.substr(prev_start, start-prev_start);
    if (filterFunc(s)) out.push_back(s);

    return out;
}

inline uint8_t hexCharToInt(char c) {
    if ('0' <= c && c <= '9') return uint8_t(c - '0');
    if ('a' <= c && c <= 'f') return uint8_t(c - 'a' + 10);
    if ('A' <= c && c <= 'F') return uint8_t(c - 'A' + 10);
    throw std::invalid_argument(std::string("Invalid hex digit: ") + c);
}

inline std::vector<uint8_t> hexToBytes(const std::string& hex) {
    size_t len = hex.length();
    if (len % 2 != 0) {
        throw std::invalid_argument("Hex string must have even length");
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(len / 2);

    for (size_t i = 0; i < len; i += 2) {
        uint8_t high = hexCharToInt(hex[i]);
        uint8_t low  = hexCharToInt(hex[i + 1]);
        bytes.push_back((high << 4) | low);
    }

    return bytes;
}

static inline std::string bytesToHexLower(const std::vector<uint8_t>& input)
{
    static const char* const lut = "0123456789abcdef";
    size_t len = input.size();

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

static inline std::string MD5ToHexLower(const std::array<uint8_t, 16>& input)
{
    static const char* const lut = "0123456789abcdef";
    size_t len = input.size();

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

#endif //STRINGUTILS_H
