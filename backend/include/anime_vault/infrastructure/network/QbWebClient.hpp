#pragma once

#include "anime_vault/services/MikanTitleParser.hpp"
#include <drogon/HttpClient.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace anime_vault {
struct QbStatus {
    bool configured{};
    bool connected{};
    std::string errorCode;
    std::string version;
    int torrentCount{};
    int completedCount{};
};

using QbMikanCatalog = MikanCatalog;
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

class QbWebClient final {
public:
    using Completion = std::function<void(QbStatus)>;
    QbWebClient(std::string baseUrl, std::string username, std::string password);
    void inspect(Completion completion) const;
    using RssCompletion = std::function<void(QbMikanCatalog)>;
    void readMikanTitles(RssCompletion completion) const;
    using ActionCompletion = std::function<void(QbActionResult)>;
    void addMikanFeed(std::string url, ActionCompletion completion) const;
    void addMikanFeedWithAutoRule(std::string url, std::string savePath,
                                  ActionCompletion completion) const;
    void createMikanRule(QbMikanRuleSpec spec, ActionCompletion completion) const;
    static std::optional<std::string> loginCookie(int status, std::string_view body,
                                                  std::string_view setCookie);
    static std::optional<std::string> loginCookie(const drogon::HttpResponsePtr& response);
    static QbMikanCatalog parseMikanItems(std::string_view body);
    static bool validMikanFeedUrl(std::string_view url);
    static std::string mikanFeedForm(const std::string& url);
    static std::string mikanRuleJson(const QbMikanRuleSpec& spec);
    static std::string autoMikanRuleJson(const QbMikanRuleSpec& spec, bool enabled = true);
    static std::string bracketEpisodePattern();
    static std::string dashedEpisodePattern();
    static std::string autoEpisodePreferencesJson(std::string_view currentPreferences);
    static std::optional<std::string> readyMikanFeedPath(std::string_view items,
                                                          std::string_view url);
    bool configured() const noexcept;
    const std::string& baseUrl() const noexcept { return baseUrl_; }
private:
    std::string baseUrl_, username_, password_;
    drogon::HttpClientPtr client_;
};
}
