#pragma once

#include "anime_vault/domain/Qb.hpp"
#include "anime_vault/services/MikanTitleParser.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace anime_vault {
using QbMikanCatalog = MikanCatalog;

// qB RSS 的确定性解析与规则构造，可脱离网络适配器独立使用。
class QbRssRules final {
public:
    static QbMikanCatalog parseMikanItems(std::string_view body);
    static bool validMikanFeedUrl(std::string_view url);
    static std::string mikanFeedForm(const std::string& url);
    static std::string mikanRuleJson(const QbMikanRuleSpec& spec);
    static std::string autoMikanRuleJson(const QbMikanRuleSpec& spec, bool enabled = true);
    static std::string autoMikanHistoryJson(const QbMikanRuleSpec& spec, std::string_view rules,
                                            std::string_view items, std::string_view torrents);
    static std::string bracketEpisodePattern();
    static std::string dashedEpisodePattern();
    static std::string autoEpisodePreferencesJson(std::string_view currentPreferences);
    static std::optional<std::string> readyMikanFeedPath(std::string_view items,
                                                          std::string_view url);
};
}
