//
// Created by Deamon on 4/24/2025.
//

#ifndef TACTCONFIGPARSER_H
#define TACTCONFIGPARSER_H
#include <functional>
#include <string>
#include <vector>


class TactConfigParser {
public:
    /**
     * @param configText        Raw text containing | delimited config (e.g. downloaded from URL).
     * @param headerSearchTerms List of header prefixes to find in the first line (e.g. {"Name","Path","Hosts"}).
     * @param region            Filter value for the first column header (usually settings_.Region).
     */
    static void parse(
        const std::string& configText,
        const std::vector<std::string>& headerSearchTerms,
        const std::function<bool (std::unordered_map<std::string, std::string>&)> callback
    );
};

#endif //TACTCONFIGPARSER_H
