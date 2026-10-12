#include "anime_vault/services/MikanTitleParser.hpp"
#include "anime_vault/services/ReleaseTextNormalization.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <algorithm>
#include <charconv>
#include <regex>

namespace anime_vault {
namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
bool chineseName(const std::string& value) {
    const auto points = normalizeTitle(value);
    bool han = false;
    for (auto point : points) {
        if (point >= 0x3040 && point <= 0x30ff) return false;
        han = han || (point >= 0x3400 && point <= 0x9fff);
    }
    return han;
}
}

std::vector<MikanTitlePair> MikanTitleParser::parseAll(std::string_view articleTitle) {
    if (articleTitle.empty() || articleTitle.size() > 2048) return {};
    auto text = trim(normalizeReleaseText(articleTitle));
    if (text.empty() || text.front() != '[') return {};
    const auto groupEnd = text.find(']');
    if (groupEnd == std::string::npos || groupEnd > 300) return {};
    text = trim(text.substr(groupEnd + 1));
    static const std::regex announcement(R"(^★[0-9]{1,2}月新番★\s*)");
    text = std::regex_replace(text, announcement, "");
    // 发布标签可有多个；包住整个双语标题的方括号必须保留到拆分阶段。
    while (!text.empty() && text.front() == '[') {
        const auto end = text.find(']');
        if (end == std::string::npos) return {};
        const auto content = text.substr(1, end - 1);
        if (content.find('/') != std::string::npos || content.find('_') != std::string::npos) break;
        const auto remainder = trim(text.substr(end + 1));
        if (remainder.empty() || remainder.front() == '[') break;
        text = remainder;
    }
    static const std::regex bracketEpisode(R"(^(.+?)\s*\[\s*([0-9]{1,3})(?:v[0-9]+)?\s*\]\s*((?:\[[^\]]*\]\s*)*(?:\([^)]*\))?)$)", std::regex::icase);
    static const std::regex dashedEpisode(R"(^(.+?)\s+-\s*([0-9]{1,3})(?:v[0-9]+)?\s*(?:-\s*)?((?:\[[^\]]*\]\s*)+|\([^)]*\)|)$)", std::regex::icase);
    static const std::regex seasonEpisode(R"(^(.+?)\s+S[0-9]{1,2}E([0-9]{1,3})\s*((?:\[[^\]]*\]\s*)+)$)", std::regex::icase);
    std::smatch match;
    if (!std::regex_match(text, match, bracketEpisode) &&
        !std::regex_match(text, match, dashedEpisode) &&
        !std::regex_match(text, match, seasonEpisode)) return {};
    int episode{};
    const auto number = match[2].str();
    const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), episode);
    if (error != std::errc{} || end != number.data() + number.size() || episode < 1) return {};
    auto titles = trim(match[1].str());
    std::string bracketedAlias;
    if (titles.size() > 1 && titles.front() == '[' && titles.back() == ']') {
        titles = titles.substr(1, titles.size() - 2);
        bracketedAlias = titles;
        // 仅括号包裹的中英标题采用下划线双语分隔，普通罗马音中的下划线仍是空白。
        if (titles.find('/') == std::string::npos && chineseName(titles))
            std::replace(titles.begin(), titles.end(), '_', '/');
    }
    std::vector<std::size_t> separators;
    const auto numericSlash = [&titles](std::size_t i) {
        return i > 0 && i + 1 < titles.size() && titles[i - 1] >= '0' && titles[i - 1] <= '9' &&
            titles[i + 1] >= '0' && titles[i + 1] <= '9';
    };
    for (std::size_t i = 1; i + 1 < titles.size(); ++i)
        if (titles[i] == '/' && !numericSlash(i) && (titles[i - 1] == ' ' || titles[i + 1] == ' ')) separators.push_back(i);
    // 优先使用明确的语言分隔，保留 Fate/Zero 等名称内部的紧凑斜杠。
    if (separators.empty()) {
        const auto latin = [](unsigned char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'); };
        for (std::size_t i = 1; i + 1 < titles.size(); ++i) {
            if (titles[i] != '/' || numericSlash(i)) continue;
            const auto left = static_cast<unsigned char>(titles[i - 1]);
            const auto right = static_cast<unsigned char>(titles[i + 1]);
            if ((left >= 0x80 && latin(right)) || (latin(left) && right >= 0x80)) separators.push_back(i);
        }
        if (separators.size() != 1) return {};
    }
    separators.push_back(titles.size());
    std::vector<std::string> names;
    std::size_t start = 0;
    for (const auto i : separators) {
        auto name = trim(titles.substr(start, i - start));
        if (name.empty() || name.size() > 500 || normalizedTitleKey(name).empty()) return {};
        names.push_back(std::move(name));
        start = i + 1;
    }
    if (names.size() < 2 || names.size() > 6) return {};
    const auto chinese = std::find_if(names.begin(), names.end(), chineseName);
    const auto& canonical = chinese == names.end() ? names.front() : *chinese;
    std::vector<MikanTitlePair> pairs;
    for (auto name : names) {
        while (!name.empty() && name.back() == '.') name.pop_back();
        name = trim(name);
        const auto key = normalizedTitleKey(name);
        if (key.empty() || key == normalizedTitleKey(canonical)) continue;
        if (std::any_of(pairs.begin(), pairs.end(), [&key](const auto& pair) { return normalizedTitleKey(pair.alias) == key; })) continue;
        pairs.push_back({canonical, std::move(name), episode});
    }
    if (!pairs.empty() && !bracketedAlias.empty() && bracketedAlias.size() <= 500)
        pairs.push_back({canonical, std::move(bracketedAlias), episode});
    return pairs;
}

std::optional<MikanTitlePair> MikanTitleParser::parse(std::string_view title) {
    auto pairs = parseAll(title);
    if (pairs.empty()) return std::nullopt;
    return std::move(pairs.front());
}
}
