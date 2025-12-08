//
// Created by Deamon on 4/23/2025.
//

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

#include "CDN.h"

// Helper: trim whitespace from both ends of a string
inline std::string trim(const std::string& s) {
    auto ws_front = std::find_if_not(s.begin(), s.end(), ::isspace);
    auto ws_back  = std::find_if_not(s.rbegin(), s.rend(), ::isspace).base();
    return (ws_front < ws_back ? std::string(ws_front, ws_back) : std::string());
}

// Helper: split a string on a delimiter into a vector of strings
inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

class TactConfig {
public:
    // map from key to array of values
    std::unordered_map<std::string, std::vector<std::string>> Values;

    ~TactConfig() {
        //__debugbreak();
        Values.clear();
        std::cout << "Config destroyed" << std::endl;
    }

    explicit TactConfig(const std::string& configContent) {
        std::vector<std::string> lines;
        lines = split(configContent, '\n');

        parseConfig(lines);
    }
    explicit TactConfig(CDN& cdn, const std::string& pathOrHash, bool isFile) {
        std::vector<std::string> lines;

        if (!isFile) {
            // load raw bytes from CDN and decode as UTF-8
            const std::string & hash = pathOrHash;

            std::vector<uint8_t> data = cdn.GetFile("config", hash);
            std::string text(data.begin(), data.end());
            lines = split(text, '\n');
        }
        else {
            // read lines from local file
            const std::string & path = pathOrHash;

            std::ifstream file(path);
            if (!file) {
                throw std::runtime_error("Unable to open config file: " + path);
            }
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
        }
        parseConfig(lines);
    }

    void parseConfig(std::vector<std::string> &lines) {// parse each line
        for (auto& rawLine : lines) {
            auto parts = split(rawLine, '=');
            if (parts.size() > 1) {
                std::string key   = trim(parts[0]);
                std::string value = trim(parts[1]);
                // split on spaces into tokens
                std::vector<std::string> tokens = split(value, ' ');
                // trim each token
                for (auto& tok : tokens) {
                    tok = trim(tok);
                }
                Values.emplace(key, tokens);
            }
        }
    }
};

#endif //CONFIG_H
