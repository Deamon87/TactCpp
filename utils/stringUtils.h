//
// Created by Deamon on 4/23/2025.
//

#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <string>


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

#endif //STRINGUTILS_H
