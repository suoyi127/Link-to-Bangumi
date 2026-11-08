#include "anime_vault/services/MikanDownloads.hpp"
#include "anime_vault/services/FilenameParser.hpp"
#include "anime_vault/services/MikanChinese.hpp"
#include "anime_vault/services/MikanTitleParser.hpp"
#include "anime_vault/services/ReleaseTextNormalization.hpp"
#include "anime_vault/services/TitleNormalization.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <regex>
#include <set>

namespace anime_vault {
namespace {
using Json = nlohmann::json;
const Json *findFeed(const Json &tree, const std::string &url, int depth = 0) {
    if (depth > 10 || !tree.is_object())
        return nullptr;
    if (tree.contains("url"))
        return tree.value("url", "") == url ? &tree : nullptr;
    for (const auto &child : tree)
        if (const auto *node = findFeed(child, url, depth + 1))
            return node;
    return nullptr;
}
bool allowedTorrent(const std::string &url) {
    static const std::regex pattern(
        R"(^(?:https://(?:www\.)?mikanani\.(?:me|kas\.pub)/[A-Za-z0-9_%./?=&+-]+|magnet:\?xt=urn:btih:(?:[0-9a-f]{40}|[a-z2-7]{32})(?:&[^\s]*)?)$)",
        std::regex::icase);
    return url.size() <= 4096 && std::regex_match(url, pattern);
}
struct Release {
    MikanCandidate candidate;
    std::string key;
    ParseResult parsed;
};
} // namespace
bool isMikanSingleSeriesFeed(const std::string &url) {
    static const std::regex singleFeed(
        R"(^https://(?:www\.)?mikanani\.(?:me|kas\.pub)/RSS/Bangumi\?bangumiId=[0-9]+(?:&subgroupid=[0-9]+)?$)",
        std::regex::icase);
    return std::regex_match(url, singleFeed);
}
int mikanPreferenceScore(const MikanSubscription &feed, const std::string &groupName, const std::string &resolution,
                         int subtitleScripts) {
    const auto wanted = mikanTitleKey(feed.group);
    const auto group = mikanTitleKey(groupName);
    const bool sameGroup = !wanted.empty() && !group.empty() &&
                           (group.find(wanted) != std::string::npos || wanted.find(group) != std::string::npos);
    const int wantedScript = feed.subtitle == "chs" ? 1 : feed.subtitle == "cht" ? 2 : 0;
    return sameGroup + (!feed.resolution.empty() && resolution == feed.resolution) +
           ((subtitleScripts & wantedScript) != 0);
}
std::string resolveMikanIdentity(const std::string &identity, const MikanAliases &aliases) {
    auto parts = Json::parse(identity);
    parts[0] = foldMikanChinese(parts.at(0).get<std::string>());
    const auto it = aliases.find(parts.at(0).get<std::string>());
    if (it != aliases.end() && it->second.size() == 1)
        parts[0] = *it->second.begin();
    return parts.dump();
}
std::vector<MikanCandidate> selectMikanDownloads(const MikanSubscription &feed, const std::string &items,
                                                 const std::string &tasks, MikanAliases *knownAliases,
                                                 std::map<std::string, int> *observedScripts,
                                                 const std::set<std::string> *baseline) {
    auto tree = Json::parse(items);
    const auto *node = findFeed(tree, feed.url);
    if (!node || node->value("hasError", false) || node->value("isLoading", false) || !node->contains("articles") ||
        !(*node)["articles"].is_array())
        return {};
    std::set<std::string> names, hashes;
    for (const auto &task : Json::parse(tasks)) {
        names.insert(task.value("name", ""));
        hashes.insert(normalizedTitleKey(task.value("hash", "")));
    }
    // 只有蜜柑单番源的官方标题可以补足不同字幕组的译名；聚合源继续按每篇文章区分作品。

    const auto feedTitle = node->value("title", "");
    const std::string prefix = "Mikan Project - ";
    std::string canonicalFeed;
    if (isMikanSingleSeriesFeed(feed.url) && feedTitle.starts_with(prefix))
        canonicalFeed = mikanTitleKey(feedTitle.substr(prefix.size()));
    std::vector<Release> releases;
    MikanAliases temporary;
    auto &aliases = knownAliases ? *knownAliases : temporary;
    static const std::regex excluded(
        R"(\b(?:REPACK|PROPER|PV|NCOP|NCED|BATCH|COMPLETE)\b|合集|全集|(?:\[\s*| -\s*)[0-9]{1,3}\s*[-~+]\s*[0-9]{1,3}(?:\s*[\]\[ ]|$))",
        std::regex::icase);
    static const std::regex version(
        R"((?:\b[Ss]\d{1,2}[Ee]\d{1,3}|\b[Ee][Pp]?\d{1,3}|\d{1,3})[vV]([1-9][0-9]?)(?=[\]\s.(]|$))");
    static const std::regex cross(R"(\b(\d{1,2})x(\d{1,3})(?=[vV\s\[(]|$))", std::regex::icase);
    static const std::regex seasonSuffix(R"((?:\s+(?:Season\s*|S)(\d{1,2})|\s*第\s*(\d{1,2})\s*季)\s*$)",
                                         std::regex::icase);
    auto parse = [&](std::string title, std::string url, bool existing) {
        Release release;
        auto &candidate = release.candidate;
        candidate.title = std::move(title);
        candidate.url = std::move(url);
        candidate.downloaded = existing;
        if (candidate.title.size() > 2048 || (!existing && !allowedTorrent(candidate.url)))
            return;
        std::string raw;
        candidate.subtitleScripts = mikanSubtitleScripts(candidate.title, &raw);
        if (!existing && observedScripts && candidate.subtitleScripts)
            (*observedScripts)[candidate.url] = candidate.subtitleScripts;
        if (std::regex_search(raw, excluded))
            return;
        std::smatch revision;
        candidate.revision = 1;
        if (std::regex_search(raw, revision, version)) {
            candidate.revision = std::stoi(revision[1].str());
            raw.erase(revision.position(1) - 1, revision.length(1) + 1);
        }
        raw = std::regex_replace(raw, cross, "S$1E$2");
        const auto pairs = MikanTitleParser::parseAll(raw);
        // 发布标题的斜线可分隔双语番名，传入文件名解析器前先规范化。
        std::replace(raw.begin(), raw.end(), '/', ' ');
        std::replace(raw.begin(), raw.end(), ':', ' ');
        release.parsed = FilenameParser{}.parse(raw);
        const auto &parsed = release.parsed;
        if (!parsed.episode || parsed.confidence < 0.9 || parsed.episodeType == EpisodeType::ncop ||
            parsed.episodeType == EpisodeType::nced)
            return;
        auto titleKey = pairs.empty() ? parsed.title : pairs.front().canonicalTitle;
        std::smatch season;
        if (std::regex_search(titleKey, season, seasonSuffix)) {
            auto number = static_cast<std::uint32_t>(std::stoul(season[1].matched ? season[1].str() : season[2].str()));
            if (parsed.season && *parsed.season != number)
                return;
            release.parsed.season = number;
            titleKey.erase(season.position());
        }
        release.key = mikanTitleKey(titleKey);
        if (release.key.empty())
            return;
        if (!existing && !canonicalFeed.empty() && release.key != canonicalFeed) {
            aliases[release.key].insert(canonicalFeed);
            release.key = canonicalFeed;
        }
        if (!existing)
            for (const auto &pair : pairs)
                aliases[mikanTitleKey(pair.alias)].insert(release.key);
        candidate.group = parsed.releaseGroup.value_or("");
        candidate.resolution = parsed.resolution.value_or("");
        candidate.score = mikanPreferenceScore(feed, candidate.group, candidate.resolution, candidate.subtitleScripts);
        candidate.downloaded = existing || names.contains(candidate.title);
        static const std::regex btih(R"((?:urn:btih:|/)([0-9a-f]{40})(?:[&./?]|$))", std::regex::icase);
        std::smatch hash;
        if (std::regex_search(candidate.url, hash, btih) && hashes.contains(normalizedTitleKey(hash[1].str())))
            candidate.downloaded = true;
        releases.push_back(std::move(release));
    };
    for (const auto &article : (*node)["articles"])
        if (article.is_object())
            parse(article.value("title", ""), article.value("torrentURL", ""), false);
    const auto feedReleaseCount = releases.size();
    for (const auto &task : Json::parse(tasks))
        if (task.is_object())
            parse(task.value("name", ""), "qb-task:" + task.value("hash", task.value("name", "")), true);
    std::map<std::string, MikanCandidate> best;
    for (std::size_t i = 0; i < releases.size(); ++i) {
        const auto &release = releases[i];
        auto candidate = release.candidate;
        auto key = release.key;
        if (const auto alias = aliases.find(key); alias != aliases.end() && alias->second.size() == 1)
            key = *alias->second.begin();
        candidate.identity =
            Json::array({key, release.parsed.season.value_or(1), static_cast<int>(release.parsed.episodeType),
                         release.parsed.episode->toString()})
                .dump();
        if (i < feedReleaseCount && baseline && baseline->contains(candidate.url))
            continue;
        if (i >= feedReleaseCount && !best.contains(candidate.identity))
            continue;
        auto [it, inserted] = best.try_emplace(candidate.identity, candidate);
        // 默认保留已下载版本；指定偏好后按得分、修订版择优，同分优先已有任务。
        const bool ranked = feed.hasPreferences();
        const auto rank = [ranked](const MikanCandidate &c) {
            return std::tuple{ranked ? c.score : 0, ranked ? c.revision : 0, c.downloaded};
        };
        if (!inserted && rank(candidate) > rank(it->second))
            it->second = candidate;
    }
    std::vector<MikanCandidate> result;
    for (auto &entry : best)
        result.push_back(std::move(entry.second));
    return result;
}
} // namespace anime_vault
