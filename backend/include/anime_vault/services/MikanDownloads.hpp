#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

namespace anime_vault {
struct MikanSubscription {
    std::string url, group, resolution, subtitle;
    bool hasPreferences() const { return !group.empty() || !resolution.empty() || !subtitle.empty(); }
};
struct MikanCandidate {
    std::string identity, url, title, group, resolution;
    int score{}, revision{};
    bool downloaded{};
    int subtitleScripts{};
};
bool isMikanSingleSeriesFeed(const std::string &url);
int mikanPreferenceScore(const MikanSubscription &feed, const std::string &group, const std::string &resolution,
                         int subtitleScripts);
using MikanAliases = std::map<std::string, std::set<std::string>>;
std::string resolveMikanIdentity(const std::string &identity, const MikanAliases &aliases);
std::vector<MikanCandidate> selectMikanDownloads(const MikanSubscription &feed, const std::string &items,
                                                 const std::string &tasks, MikanAliases *knownAliases = nullptr,
                                                 std::map<std::string, int> *observedScripts = nullptr,
                                                 const std::set<std::string> *baseline = nullptr);
} // namespace anime_vault
