#pragma once

#include <string>
#include <vector>

namespace anime_vault {
struct QbStatus {
    bool configured{};
    bool connected{};
    std::string errorCode;
    std::string version;
    int torrentCount{};
    int completedCount{};
    std::vector<std::string> completedPaths;
};

struct QbActionResult {
    bool success{};
    std::string errorCode;
};
struct QbMikanRuleSpec {
    std::string ruleName;
    std::string feedUrl;
    std::string keyword;
    std::string savePath;
};
struct QbEndpoint { std::string address, hostHeader; int port{}; };

}
