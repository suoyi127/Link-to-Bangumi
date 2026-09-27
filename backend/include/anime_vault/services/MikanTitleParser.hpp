#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace anime_vault {
struct MikanTitlePair {
    std::string canonicalTitle;
    std::string alias;
    int episode{};
};

struct MikanCatalog {
    int feedCount{};
    int articleCount{};
    std::vector<MikanTitlePair> pairs;
    std::string errorCode;
    struct FeedSummary {
        std::string title;
        int articleCount{};
        bool hasError{};
    };
    std::vector<FeedSummary> feeds;
};

// Parses bilingual RSS metadata only; it never describes the downloaded filename.
class MikanTitleParser final {
public:
    static std::optional<MikanTitlePair> parse(std::string_view articleTitle);
};
}
