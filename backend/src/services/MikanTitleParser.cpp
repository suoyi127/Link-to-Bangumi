#include "anime_vault/services/MikanTitleParser.hpp"

#include <charconv>
#include <regex>

namespace anime_vault {
namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
}

std::optional<MikanTitlePair> MikanTitleParser::parse(std::string_view articleTitle) {
    if (articleTitle.empty() || articleTitle.size() > 2048) return std::nullopt;
    // 只识别明确的双语分隔和集数格式；无法可靠拆分时宁可不自动建立别名。
    static const std::regex pattern(
        R"(^\[[^\]]{1,100}\]\s*(.+?)\s+/\s+(.+?)\s+\[([0-9]{1,3})\](?:\[[^\]]*\])*\s*$)");
    static const std::regex dashedPattern(
        R"(^\[[^\]]{1,100}\]\s*(.+?)\s+/\s+(.+?)\s+/\s+(.+?)\s+-\s+([0-9]{1,3})\s+-\s+(?:\[[^\]]*\])+\s*$)");
    const std::string text(articleTitle);
    std::smatch match;
    std::size_t episodeGroup = 3;
    if (!std::regex_match(text, match, pattern)) {
        if (!std::regex_match(text, match, dashedPattern)) return std::nullopt;
        episodeGroup = 4;
    }
    auto canonical = trim(match[1].str());
    auto alias = trim(match[2].str());
    while (!alias.empty() && alias.back() == '.') alias.pop_back();
    alias = trim(alias);
    int episode{};
    const auto number = match[episodeGroup].str();
    const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), episode);
    if (error != std::errc{} || end != number.data() + number.size() || episode < 1 ||
        canonical.empty() || alias.empty() || canonical.size() > 500 || alias.size() > 500 ||
        canonical == alias) return std::nullopt;
    return MikanTitlePair{std::move(canonical), std::move(alias), episode};
}
}
