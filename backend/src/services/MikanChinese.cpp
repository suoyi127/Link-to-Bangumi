#include "anime_vault/services/MikanChinese.hpp"
#include "anime_vault/services/TitleNormalization.hpp"
#include "anime_vault/services/ReleaseTextNormalization.hpp"
#include <algorithm>
#include <regex>
#include <unordered_map>

namespace anime_vault {
namespace {
constexpr std::string_view characters[] = {
#include "TSCharacters.inc"
};
constexpr std::string_view phrases[] = {
#include "TSPhrases.inc"
};
const auto &variants() {
    static const auto table = [] {
        std::unordered_map<std::string_view, std::string_view> map;
        for (auto row : characters) {
            const auto split = row.find('\t');
            // 多个候选需要语境才能确定，保留原字，避免把不同番名合并。
            if (split != row.npos && row.find(' ', split) == row.npos)
                map.emplace(row.substr(0, split), row.substr(split + 1));
        }
        return map;
    }();
    return table;
}
} // namespace
std::string foldMikanChinese(std::string_view text) {
    const auto &map = variants();
    std::string result;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t end = i + 1;
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
            ++end;
        auto ch = text.substr(i, end - i);
        const auto it = map.find(ch);
        result.append(it == map.end() ? ch : it->second);
        i = end;
    }
    return result;
}
std::string mikanTitleKey(std::string_view text) { return normalizedTitleKey(foldMikanChinese(text)); }
std::string foldScrapeTitle(std::string_view title) {
    // 日文原题中的汉字与假名共同构成作品名，保留原字形供搜索和比对。
    if (std::all_of(title.begin(), title.end(), [](unsigned char c) { return c < 0x80; }))
        return std::string(title);
    const auto points = normalizeTitle(title);
    const bool japanese = std::any_of(points.begin(), points.end(), [](char32_t c) {
        return (c >= 0x3040 && c <= 0x30ff) || (c >= 0xff66 && c <= 0xff9f);
    });
    if (japanese || points.empty()) return std::string(title);
    // 最长词组优先，保留「乾隆」等专名；单字采用字典首选简体。
    static const auto dictionary = [] {
        std::unordered_map<std::string_view, std::string_view> entries;
        std::size_t longest = 0;
        const auto add = [&](std::string_view row) {
            const auto tab = row.find('\t');
            if (tab == row.npos) return;
            const auto values = row.substr(tab + 1);
            entries[row.substr(0, tab)] = values.substr(0, values.find(' '));
            longest = std::max(longest, tab);
        };
        for (auto row : characters) add(row);
        for (auto row : phrases) add(row);
        return std::pair{std::move(entries), longest};
    }();
    std::string result;
    while (!title.empty()) {
        std::size_t width = 1;
        while (width < title.size() && (static_cast<unsigned char>(title[width]) & 0xc0) == 0x80) ++width;
        auto match = title.substr(0, width);
        for (auto n = std::min(dictionary.second, title.size()); n >= width; --n) {
            const auto found = dictionary.first.find(title.substr(0, n));
            if (found != dictionary.first.end()) {
                width = n;
                match = found->second;
                break;
            }
        }
        result.append(match);
        title.remove_prefix(width);
    }
    return result;
}
std::string scrapeTitleKey(std::string_view title) {
    return normalizedTitleKey(foldScrapeTitle(title));
}
std::u32string normalizeScrapeTitle(std::string_view title) {
    return normalizeTitle(foldScrapeTitle(title));
}
int mikanSubtitleScripts(std::string_view title, std::string *cleanTitle) {
    // 识别独立字幕标签和集号后的发布说明，字幕标签不属于番名。
    const auto raw = normalizeReleaseText(title);
    static const std::regex episode(
        R"((?:\[\s*(?:SP|OVA)?[0-9]{1,3}(?:v[0-9]+)?\s*\]|(?:^|[\s_-])(?:S\d{1,2}E|EP?|\d{1,2}x|SP|OVA|-\s*)(?:\d{1,3}(?:\.\d)?)(?:v[0-9]+)?(?=$|[\s_\[(.])))",
        std::regex::icase);
    std::smatch match;
    std::string tags = std::regex_search(raw, match, episode) ? foldMikanChinese(match.suffix().str()) : "";
    static const std::regex bracket(R"(\[([^\[\]]{1,80})\])");
    static const std::regex explicitTag(
        R"((?:(?:CHS|CHT|SC|TC|GB2312|GB18030|GB|BIG5|zh[-_]?(?:Hans|CN|Hant|TW|HK)|简繁|繁简|简体|繁体|简日|繁日|简中|繁中|简|繁)[ /&+_-]*)+(?:字幕|内封|内嵌|双语)?)",
        std::regex::icase);
    std::size_t copied = 0;
    if (cleanTitle)
        cleanTitle->clear();
    for (std::sregex_iterator it(raw.begin(), raw.end(), bracket), end; it != end; ++it) {
        const auto token = foldMikanChinese((*it)[1].str());
        if (!std::regex_match(token, explicitTag))
            continue;
        tags += " [" + token + "]";
        if (cleanTitle) {
            auto pos = static_cast<std::size_t>(it->position());
            cleanTitle->append(raw, copied, pos - copied);
            cleanTitle->push_back(' ');
            copied = pos + it->length();
        }
    }
    if (cleanTitle)
        cleanTitle->append(raw, copied);
    static const std::regex fileSize(R"(\b\d+(?:\.\d+)?\s*GB\b)", std::regex::icase);
    tags = std::regex_replace(tags, fileSize, " ");
    static const std::regex simplified(
        R"((?:^|[^A-Za-z0-9])(?:CHS|SC|GB|GB2312|GB18030|zh[-_]?(?:Hans|CN))(?:$|[^A-Za-z0-9])|简体|简中|简日|简繁|繁简|(?:^|[\[(/&+_ -])简(?:$|[\])/&+_ -]|内封|内嵌|字幕|双语))",
        std::regex::icase);
    static const std::regex traditional(
        R"((?:^|[^A-Za-z0-9])(?:CHT|TC|BIG5|zh[-_]?(?:Hant|TW|HK))(?:$|[^A-Za-z0-9])|繁体|繁中|繁日|简繁|繁简|(?:^|[\[(/&+_ -])繁(?:$|[\])/&+_ -]|内封|内嵌|字幕|双语))",
        std::regex::icase);
    return (std::regex_search(tags, simplified) ? 1 : 0) | (std::regex_search(tags, traditional) ? 2 : 0);
}
} // namespace anime_vault
