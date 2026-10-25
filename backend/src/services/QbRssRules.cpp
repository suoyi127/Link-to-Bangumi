#include "anime_vault/services/QbRssRules.hpp"
#include "anime_vault/services/TitleNormalization.hpp"
#include "anime_vault/services/FilenameParser.hpp"
#include "anime_vault/services/ReleaseTextNormalization.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace anime_vault {
namespace {
std::string formEncode(const std::string& value) {
    const char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '*')
            encoded += static_cast<char>(ch);
        else {
            encoded += '%';
            encoded += hex[ch >> 4];
            encoded += hex[ch & 15];
        }
    }
    return encoded;
}
}

QbMikanCatalog QbRssRules::parseMikanItems(std::string_view body) {
    if (body.empty() || body.size() > 4 * 1024 * 1024)
        throw std::invalid_argument("invalid qB RSS response size");
    const auto root = nlohmann::json::parse(body);
    if (!root.is_object()) throw std::invalid_argument("invalid qB RSS response");
    QbMikanCatalog result;
    std::unordered_map<std::string, MikanTitlePair> unique;
    std::unordered_set<std::string> conflicts;
    std::unordered_map<std::string, std::string> owners;
    static const std::regex mikanUrl(R"(^https://(?:www\.)?mikanani\.(?:me|kas\.pub)/RSS/)",
                                     std::regex::icase);
    std::function<void(const nlohmann::json&, int)> visit = [&](const nlohmann::json& node, int depth) {
        if (depth > 8 || !node.is_object()) return;
        const auto url = node.value("url", std::string{});
        if (!url.empty() && std::regex_search(url, mikanUrl) && node.contains("articles") &&
            node["articles"].is_array()) {
            ++result.feedCount;
            result.feeds.push_back({node.value("title", std::string{}),
                static_cast<int>(node["articles"].size()), node.value("hasError", false)});
            for (const auto& article : node["articles"]) {
                if (++result.articleCount > 5000) throw std::invalid_argument("too many RSS articles");
                if (!article.is_object() || !article.contains("title") ||
                    !article["title"].is_string()) continue;
                const auto title = article["title"].get<std::string>();
                auto pairs = MikanTitleParser::parseAll(title);
                // 仅单番订阅可用 feed 标题提供季数及译名；搜索/聚合订阅仍只采文章中的明确双语关系。
                std::string canonical;
                static const std::regex singleFeed(R"(^https://(?:www\.)?mikanani\.(?:me|kas\.pub)/RSS/Bangumi\?bangumiId=[0-9]+(?:&subgroupid=[0-9]+)?$)", std::regex::icase);
                const auto feedTitle = node.value("title", std::string{});
                constexpr std::string_view prefix = "Mikan Project - ";
                if (!node.value("hasError", false) && std::regex_match(url, singleFeed) && feedTitle.starts_with(prefix))
                    canonical = feedTitle.substr(prefix.size());
                if (!canonical.empty() && canonical.size() <= 500) {
                    const auto add = [&](const std::string& alias, int episode) {
                        if (!alias.empty() && alias.size() <= 500 && normalizedTitleKey(alias) != normalizedTitleKey(canonical))
                            pairs.push_back({canonical, alias, episode, true});
                    };
                    const auto originalPairs = pairs;
                    for (auto& pair : pairs) { pair.canonicalTitle = canonical; pair.feedScoped = true; }
                    for (const auto& pair : originalPairs) {
                        add(pair.canonicalTitle, pair.episode);
                        const auto aliasPoints = normalizeTitle(pair.alias);
                        if (std::none_of(aliasPoints.begin(), aliasPoints.end(), [](char32_t c) { return c >= 0x3400 && c <= 0x9fff; }))
                            add(pair.canonicalTitle + " " + pair.alias, pair.episode);
                    }
                    const auto parsed = FilenameParser{}.parse(title);
                    if (parsed.confidence >= 0.9 && parsed.episode) add(parsed.title, static_cast<int>(parsed.episode->whole()));
                    // 兼容旧版把发布标签和半个括号保存成标题的记录，使其也能通过本次订阅纠正。
                    const auto normalized = normalizeReleaseText(title);
                    const auto groupEnd = normalized.find(']'), slash = normalized.find('/');
                    if (!originalPairs.empty() && groupEnd != std::string::npos && slash != std::string::npos && slash > groupEnd) {
                        auto legacy = normalized.substr(groupEnd + 1, slash - groupEnd - 1);
                        const auto first = legacy.find_first_not_of(" ");
                        if (first != std::string::npos) legacy = legacy.substr(first, legacy.find_last_not_of(" ") - first + 1);
                        add(legacy, originalPairs.front().episode);
                    }
                }
                for (const auto& pair : pairs) {
                    const auto aliasKey = normalizedTitleKey(pair.alias);
                    if (aliasKey.empty() || conflicts.contains(aliasKey)) continue;
                    const auto canonicalKey = normalizedTitleKey(pair.canonicalTitle);
                    const auto [owner, added] = owners.emplace(aliasKey, canonicalKey);
                    if (!added && owner->second != canonicalKey) { conflicts.insert(aliasKey); continue; }
                    const auto key = aliasKey + (pair.feedScoped ? ":episode:" + std::to_string(pair.episode) : "");
                    unique.try_emplace(key, pair);
                }
            }
            return;
        }
        for (const auto& item : node.items()) visit(item.value(), depth + 1);
    };
    visit(root, 0);
    for (auto& [_, pair] : unique)
        if (!conflicts.contains(normalizedTitleKey(pair.alias))) result.pairs.push_back(std::move(pair));
    return result;
}

bool QbRssRules::validMikanFeedUrl(std::string_view url) {
    if (url.empty() || url.size() > 1024) return false;
    static const std::regex pattern(
        R"(^https://(?:www\.)?mikanani\.(?:me|kas\.pub)/RSS/(?:Bangumi|MyBangumi|Search)(?:\?[A-Za-z0-9._~%=&+-]{1,768})?$)",
        std::regex::icase);
    return std::regex_match(url.begin(), url.end(), pattern);
}

std::string QbRssRules::mikanRuleJson(const QbMikanRuleSpec& spec) {
    if (!validMikanFeedUrl(spec.feedUrl) || spec.ruleName.empty() ||
        spec.ruleName.size() > 80 || spec.keyword.empty() || spec.keyword.size() > 120 ||
        spec.savePath.empty() || spec.savePath.size() > 1024 ||
        !std::filesystem::path(spec.savePath).is_absolute())
        throw std::invalid_argument("invalid qB Mikan rule");
    for (const unsigned char ch : spec.ruleName)
        if (ch < 0x20 || ch == 0x7f) throw std::invalid_argument("invalid qB rule name");
    for (const unsigned char ch : spec.keyword)
        if (ch < 0x20 || ch == 0x7f) throw std::invalid_argument("invalid qB rule keyword");
    for (const auto& component : std::filesystem::path(spec.savePath))
        if (component == "..") throw std::invalid_argument("invalid qB save path");
    return nlohmann::json{
        {"enabled", true}, {"mustContain", spec.keyword}, {"mustNotContain", ""},
        {"useRegex", false}, {"episodeFilter", ""}, {"smartFilter", false},
        {"previouslyMatchedEpisodes", nlohmann::json::array()},
        {"affectedFeeds", nlohmann::json::array({spec.feedUrl})},
        {"ignoreDays", 0}, {"lastMatch", ""}, {"addPaused", false},
        {"assignedCategory", ""}, {"savePath", spec.savePath}
    }.dump();
}

std::string QbRssRules::bracketEpisodePattern() {
    return R"((?<=\[)0*(\d{1,3})(?:v\d+)?(?=\]))";
}

std::string QbRssRules::dashedEpisodePattern() {
    // qB 使用扩展正则模式，字面空格必须转义。
    return R"((?<=-\x20)0*(\d{1,3})(?:v\d+)?(?=\s*(?:\[|\(|-)))";
}

std::string QbRssRules::autoMikanRuleJson(const QbMikanRuleSpec& spec, bool enabled) {
    auto validated = spec;
    validated.keyword = "*";
    auto rule = nlohmann::json::parse(mikanRuleJson(validated));
    auto filters = nlohmann::json::parse(autoEpisodePreferencesJson(
        R"({"rss_smart_episode_filters":""})"))["rss_smart_episode_filters"].get<std::string>();
    std::replace(filters.begin(), filters.end(), '\n', '|');
    rule["mustContain"] = "(?:_|\\b)(?:" + filters + ")(?:_|\\b)";
    rule["mustNotContain"] = "REPACK";
    rule["useRegex"] = true;
    rule["smartFilter"] = true;
    rule["enabled"] = enabled;
    return rule.dump();
}

std::string QbRssRules::autoEpisodePreferencesJson(std::string_view currentPreferences) {
    const auto current = nlohmann::json::parse(currentPreferences);
    if (!current.is_object() || !current.contains("rss_smart_episode_filters") ||
        !current["rss_smart_episode_filters"].is_string())
        throw std::invalid_argument("invalid qB episode filters");
    // 每个分支只捕获集数，避免季度、分辨率或日期成为不同的去重键。
    const std::string filters = std::string(R"(\bs\d{1,2}e0*(\d{1,3})(?:v\d+)?\b)") + "\n" +
        R"(\b\d{1,2}x0*(\d{1,3})(?:v\d+)?\b)" + "\n" +
        R"(\bep0*(\d{1,3})(?:v\d+)?\b)" + "\n" +
        bracketEpisodePattern() + "\n" + dashedEpisodePattern();
    return nlohmann::json{{"rss_smart_episode_filters", filters},
                          {"rss_download_repack_proper_episodes", false}}.dump();
}

std::string QbRssRules::autoMikanHistoryJson(const QbMikanRuleSpec& spec, std::string_view rulesText,
                                                std::string_view itemsText, std::string_view torrentsText) {
    const auto rules = nlohmann::json::parse(rulesText);
    const auto items = nlohmann::json::parse(itemsText);
    const auto torrents = nlohmann::json::parse(torrentsText);
    if (!rules.is_object() || !items.is_object() || !torrents.is_array())
        throw std::invalid_argument("invalid qB history inputs");
    std::set<int> episodes;
    for (const auto& rule : rules) {
        if (!rule.is_object()) continue;
        const auto feeds = rule.value("affectedFeeds", nlohmann::json::array());
        // 多源旧规则可能混合季度，不复用其历史。
        if (feeds != nlohmann::json::array({spec.feedUrl})) continue;
        for (const auto& entry : rule.value("previouslyMatchedEpisodes", nlohmann::json::array())) {
            if (!entry.is_string()) continue;
            const auto value = entry.get<std::string>();
            if (value.empty() || value.size() > 3 || value.find_first_not_of("0123456789") != std::string::npos) continue;
            episodes.insert(std::stoi(value));
        }
    }
    std::unordered_set<std::string> names, hashes;
    for (const auto& torrent : torrents) {
        if (!torrent.is_object()) continue;
        const auto name = torrent.value("name", std::string{});
        names.insert(name);
        const auto extension = std::filesystem::path(name).extension().string();
        if (extension == ".mkv" || extension == ".mp4" || extension == ".avi")
            names.insert(std::filesystem::path(name).stem().string());
        auto hash = torrent.value("hash", std::string{});
        std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (hash.size() == 40 && hash.find_first_not_of("0123456789abcdef") == std::string::npos) hashes.insert(hash);
    }
    // std::regex 不支持后顾，等价地消费前缀；只用于已确认任务的文章。
    static const std::regex episodePattern(
        R"(\bs\d{1,2}e0*(\d{1,3})(?:v\d+)?\b|\b\d{1,2}x0*(\d{1,3})(?:v\d+)?\b|\bep0*(\d{1,3})(?:v\d+)?\b|\[0*(\d{1,3})(?:v\d+)?\]|- 0*(\d{1,3})(?:v\d+)?(?=\s*(?:\[|\(|-)))",
        std::regex::icase);
    const auto visit = [&](const auto& self, const nlohmann::json& node) -> void {
        if (!node.is_object()) return;
        if (node.contains("url")) {
            if (node.value("url", std::string{}) != spec.feedUrl) return;
            for (const auto& article : node.value("articles", nlohmann::json::array())) {
                const auto title = article.value("title", std::string{});
                bool evidence = !title.empty() && names.contains(title);
                auto url = article.value("torrentURL", std::string{});
                std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const auto start = url.find("urn:btih:");
                if (start != std::string::npos && hashes.contains(url.substr(start + 9, 40))) evidence = true;
                std::smatch match;
                if (!evidence || !std::regex_search(title, match, episodePattern)) continue;
                for (std::size_t i = 1; i < match.size(); ++i)
                    if (match[i].matched) { episodes.insert(std::stoi(match[i].str())); break; }
            }
            return;
        }
        for (const auto& child : node) self(self, child);
    };
    visit(visit, items);
    auto history = nlohmann::json::array();
    for (const int episode : episodes) history.push_back(std::to_string(episode));
    return history.dump();
}

std::optional<std::string> QbRssRules::readyMikanFeedPath(std::string_view items,
                                                             std::string_view url) {
    const auto root = nlohmann::json::parse(items);
    if (!root.is_object()) throw std::invalid_argument("invalid qB RSS items");
    for (auto it = root.begin(); it != root.end(); ++it) {
        const auto& feed = it.value();
        if (!feed.is_object() || feed.value("url", std::string{}) != url) continue;
        if (feed.value("isLoading", true) || feed.value("hasError", false) ||
            !feed.contains("articles") || !feed["articles"].is_array() ||
            feed["articles"].empty()) return std::nullopt;
        return it.key();
    }
    return std::nullopt;
}

std::string QbRssRules::mikanFeedForm(const std::string& url) {
    const auto encoded = formEncode(url);
    // qB 5.2 要求显式传入 path，即使路径默认采用订阅地址。
    return "url=" + encoded + "&path=" + encoded;
}

}
