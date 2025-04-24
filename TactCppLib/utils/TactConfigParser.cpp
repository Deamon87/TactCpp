//
// Created by Deamon on 4/24/2025.
//

#include "TactConfigParser.h"

#include <functional>
#include <unordered_map>

#include "stringUtils.h"

void TactConfigParser::parse(
    const std::string& configText,
    const std::vector<std::string>& headerSearchTerms,
    const std::function<bool (std::unordered_map<std::string, std::string>&)> callback
) {
    // Split into non-empty, non-comment lines
    auto lines = tokenizeAndFilter(configText, "\n", [](std::string& line) {
            return !line.empty() && !startsWith(line, "##");
    });

    if (lines.empty()) return;

    // Tokenize header row
    auto headerTokens = tokenize(lines[0], "|");

    // Find indices for each requested header term
    std::unordered_map<std::string,int> indices;
    for (const auto& term : headerSearchTerms) {
        for (int i = 0; i < (int)headerTokens.size(); ++i) {
            if (startsWith(headerTokens[i], term)) {
                indices[term] = i;
                break;
            }
        }
    }
    if (indices.size() != headerSearchTerms.size()) {
        // Missing one or more headers, cannot proceed
        return;
    }

    // Process each data line
    for (const auto& line : lines) {
        auto recordTokens = tokenize(line, "|");

        std::unordered_map<std::string, std::string> record;
        for (const auto& term : headerSearchTerms) {
            record[term] = recordTokens[indices[term]];
        }
        if (!callback(record))
            break;
    }
}
