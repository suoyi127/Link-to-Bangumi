#include "anime_vault/infrastructure/network/QbWebClient.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <sstream>
#include <thread>
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

QbWebClient::QbWebClient(std::string baseUrl, std::string username, std::string password)
    : baseUrl_(std::move(baseUrl)), username_(std::move(username)), password_(std::move(password)) {
    // This machine has a different server on IPv4:8080; never send qB credentials there.
    if (baseUrl_ != "http://[::1]:8080")
        throw std::invalid_argument("qB API must use the verified IPv6 loopback endpoint");
    // Drogon's URL constructor rejects IPv6 on this build; the IP/port overload works.
    if (configured()) client_ = drogon::HttpClient::newHttpClient("::1", 8080);
}

std::optional<std::string> QbWebClient::loginCookie(int status, std::string_view body,
                                                    std::string_view setCookie) {
    if (!((status == 200 && body == "Ok.") || (status == 204 && body.empty())))
        return std::nullopt;
    const auto end = setCookie.find(';');
    const auto pair = setCookie.substr(0, end);
    const auto equals = pair.find('=');
    if (equals == std::string_view::npos || equals + 1 == pair.size()) return std::nullopt;
    const auto name = pair.substr(0, equals);
    if (name != "SID") {
        constexpr std::string_view prefix = "QBT_SID_";
        if (!name.starts_with(prefix) || name.size() == prefix.size()) return std::nullopt;
        for (const char ch : name.substr(prefix.size()))
            if (ch < '0' || ch > '9') return std::nullopt;
    }
    for (const unsigned char ch : pair.substr(equals + 1))
        if (ch <= 0x20 || ch >= 0x7f || ch == '=' || ch == ',') return std::nullopt;
    return std::string(pair);
}

std::optional<std::string> QbWebClient::loginCookie(const drogon::HttpResponsePtr& response) {
    if (!response) return std::nullopt;
    const auto status = static_cast<int>(response->statusCode());
    const auto body = response->getBody();
    if (auto cookie = loginCookie(status, body, response->getHeader("Set-Cookie")))
        return cookie;
    // Drogon parses Set-Cookie into the cookie collection and omits it from headers().
    for (const auto& entry : response->cookies())
        if (auto cookie = loginCookie(status, body,
                                      entry.first + "=" + entry.second.value()))
            return cookie;
    return std::nullopt;
}

QbMikanCatalog QbWebClient::parseMikanItems(std::string_view body) {
    if (body.empty() || body.size() > 4 * 1024 * 1024)
        throw std::invalid_argument("invalid qB RSS response size");
    const auto root = nlohmann::json::parse(body);
    if (!root.is_object()) throw std::invalid_argument("invalid qB RSS response");
    QbMikanCatalog result;
    std::unordered_map<std::string, MikanTitlePair> unique;
    std::unordered_set<std::string> conflicts;
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
                const auto pair = MikanTitleParser::parse(title);
                if (!pair) continue;
                const auto key = normalizedTitleKey(pair->alias);
                if (key.empty() || conflicts.contains(key)) continue;
                const auto previous = unique.find(key);
                if (previous == unique.end()) unique.emplace(key, *pair);
                else if (normalizedTitleKey(previous->second.canonicalTitle) !=
                         normalizedTitleKey(pair->canonicalTitle)) {
                    unique.erase(previous);
                    conflicts.insert(key);
                }
            }
            return;
        }
        for (const auto& item : node.items()) visit(item.value(), depth + 1);
    };
    visit(root, 0);
    for (auto& [_, pair] : unique) result.pairs.push_back(std::move(pair));
    return result;
}

bool QbWebClient::validMikanFeedUrl(std::string_view url) {
    if (url.empty() || url.size() > 1024) return false;
    static const std::regex pattern(
        R"(^https://(?:www\.)?mikanani\.(?:me|kas\.pub)/RSS/(?:Bangumi|MyBangumi|Search)(?:\?[A-Za-z0-9._~%=&+-]{1,768})?$)",
        std::regex::icase);
    return std::regex_match(url.begin(), url.end(), pattern);
}

std::string QbWebClient::mikanRuleJson(const QbMikanRuleSpec& spec) {
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

std::string QbWebClient::bracketEpisodePattern() {
    // qB wraps patterns in word boundaries; include adjacent words so [01] can match.
    return R"(\w+\s+\[0*(\d{1,3})\]\[[^\]]*\w+)";
}

std::string QbWebClient::dashedEpisodePattern() {
    // Match subtitle titles such as "Name - 08 - [1080P]" without treating 1080P as an episode.
    return R"(\w+\s+-\s+0*([1-9][0-9]{0,2})\s+-\s+\[[^\]]*\w+)";
}

std::string QbWebClient::autoMikanRuleJson(const QbMikanRuleSpec& spec, bool enabled) {
    auto validated = spec;
    validated.keyword = "*";
    auto rule = nlohmann::json::parse(mikanRuleJson(validated));
    // Smart filtering rejects unrecognized episodes and remembers the first accepted episode.
    rule["mustContain"] = "";
    rule["smartFilter"] = true;
    rule["enabled"] = enabled;
    return rule.dump();
}

std::string QbWebClient::autoEpisodePreferencesJson(std::string_view currentPreferences) {
    const auto current = nlohmann::json::parse(currentPreferences);
    if (!current.is_object() || !current.contains("rss_smart_episode_filters") ||
        !current["rss_smart_episode_filters"].is_string())
        throw std::invalid_argument("invalid qB episode filters");
    std::string filters = current["rss_smart_episode_filters"].get<std::string>();
    std::istringstream lines(filters);
    std::string line;
    std::unordered_set<std::string> existing;
    while (std::getline(lines, line)) existing.insert(line);
    for (const auto& pattern : {bracketEpisodePattern(), dashedEpisodePattern()}) {
        if (existing.contains(pattern)) continue;
        if (!filters.empty() && filters.back() != '\n') filters += '\n';
        filters += pattern;
    }
    return nlohmann::json{{"rss_smart_episode_filters", filters},
                          {"rss_download_repack_proper_episodes", false}}.dump();
}

std::optional<std::string> QbWebClient::readyMikanFeedPath(std::string_view items,
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

bool QbWebClient::configured() const noexcept { return !username_.empty() && !password_.empty(); }

void QbWebClient::inspect(Completion completion) const {
    if (!configured()) { completion({false, false, "qb_unconfigured"}); return; }
    auto preflight = drogon::HttpRequest::newHttpRequest();
    preflight->setMethod(drogon::Get);
    preflight->setPath("/api/v2/app/version");
    preflight->addHeader("Host", "[::1]:8080");
    auto login = drogon::HttpRequest::newHttpRequest();
    login->setMethod(drogon::Post);
    login->setPath("/api/v2/auth/login");
    login->addHeader("Content-Type", "application/x-www-form-urlencoded");
    login->addHeader("Host", "[::1]:8080");
    login->addHeader("Referer", baseUrl_ + "/");
    login->setBody("username=" + formEncode(username_) + "&password=" + formEncode(password_));
    auto client = client_;
    client->sendRequest(preflight, [client, login, completion = std::move(completion), base = baseUrl_](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        // An unrelated IPv4 server also owns port 8080; reject an unexpected endpoint before login.
        if (result != drogon::ReqResult::Ok || !response ||
            (response->statusCode() != drogon::k403Forbidden && response->statusCode() != drogon::k200OK)) {
            completion({true, false, "qb_unexpected_service"}); return;
        }
        client->sendRequest(login, [client, completion = std::move(completion), base](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (result != drogon::ReqResult::Ok || !response) {
            completion({true, false, "qb_unreachable"}); return;
        }
        if (!((response->statusCode() == drogon::k200OK && response->getBody() == "Ok.") ||
              (response->statusCode() == drogon::k204NoContent && response->getBody().empty()))) {
            completion({true, false, "qb_auth_failed"}); return;
        }
        const auto cookie = loginCookie(response);
        if (!cookie) { completion({true, false, "qb_session_missing"}); return; }
        auto request = drogon::HttpRequest::newHttpRequest();
        request->setMethod(drogon::Get);
        request->setPath("/api/v2/app/version");
        request->addHeader("Host", "[::1]:8080");
        request->addHeader("Cookie", *cookie);
        request->addHeader("Referer", base + "/");
        client->sendRequest(request, [client, cookie = *cookie, completion = std::move(completion), base](
            drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
            if (result != drogon::ReqResult::Ok || !response || response->statusCode() != drogon::k200OK ||
                response->getBody().size() > 100) {
                completion({true, false, "qb_api_unavailable"}); return;
            }
            const std::string version(response->getBody());
            auto torrents = drogon::HttpRequest::newHttpRequest();
            torrents->setMethod(drogon::Get);
            torrents->setPath("/api/v2/torrents/info?filter=all");
            torrents->addHeader("Host", "[::1]:8080");
            torrents->addHeader("Cookie", cookie);
            torrents->addHeader("Referer", base + "/");
            client->sendRequest(torrents, [completion = std::move(completion), version](
                drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
                if (result != drogon::ReqResult::Ok || !response || response->statusCode() != drogon::k200OK ||
                    response->getBody().size() > 4 * 1024 * 1024) {
                    completion({true, false, "qb_api_unavailable"}); return;
                }
                try {
                    const auto data = nlohmann::json::parse(response->getBody());
                    if (!data.is_array()) throw std::invalid_argument("invalid qB response");
                    QbStatus status{true, true, "", version};
                    for (const auto& torrent : data) {
                        if (!torrent.is_object()) continue;
                        ++status.torrentCount;
                        if (torrent.value("progress", 0.0) >= 1.0)
                            ++status.completedCount;
                    }
                    completion(std::move(status));
                } catch (...) { completion({true, false, "qb_bad_response"}); }
            }, 5.0);
        }, 5.0);
        }, 5.0);
    }, 5.0);
}

void QbWebClient::readMikanTitles(RssCompletion completion) const {
    if (!configured()) { completion({0, 0, {}, "qb_unconfigured"}); return; }
    auto preflight = drogon::HttpRequest::newHttpRequest();
    preflight->setMethod(drogon::Get);
    preflight->setPath("/api/v2/app/version");
    preflight->addHeader("Host", "[::1]:8080");
    auto client = client_;
    const auto base = baseUrl_;
    const auto form = "username=" + formEncode(username_) + "&password=" + formEncode(password_);
    client->sendRequest(preflight, [client, base, form, completion = std::move(completion)](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (result != drogon::ReqResult::Ok || !response ||
            (response->statusCode() != drogon::k403Forbidden && response->statusCode() != drogon::k200OK)) {
            completion({0, 0, {}, "qb_unexpected_service"}); return;
        }
        auto login = drogon::HttpRequest::newHttpRequest();
        login->setMethod(drogon::Post);
        login->setPath("/api/v2/auth/login");
        login->addHeader("Host", "[::1]:8080");
        login->addHeader("Content-Type", "application/x-www-form-urlencoded");
        login->addHeader("Referer", base + "/");
        login->setBody(form);
        client->sendRequest(login, [client, base, completion = std::move(completion)](
            drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
            if (result != drogon::ReqResult::Ok || !response) {
                completion({0, 0, {}, "qb_unreachable"}); return;
            }
            const auto cookie = loginCookie(response);
            if (!cookie) {
                completion({0, 0, {}, "qb_auth_failed"}); return;
            }
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setMethod(drogon::Get);
            request->setPath("/api/v2/rss/items?withData=true");
            request->addHeader("Host", "[::1]:8080");
            request->addHeader("Cookie", *cookie);
            request->addHeader("Referer", base + "/");
            client->sendRequest(request, [completion = std::move(completion)](
                drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
                if (result != drogon::ReqResult::Ok || !response ||
                    response->statusCode() != drogon::k200OK) {
                    completion({0, 0, {}, "qb_api_unavailable"}); return;
                }
                QbMikanCatalog catalog;
                try { catalog = parseMikanItems(response->getBody()); }
                catch (...) { catalog.errorCode = "qb_bad_response"; }
                completion(std::move(catalog));
            }, 10.0);
        }, 5.0);
    }, 5.0);
}

void QbWebClient::addMikanFeed(std::string url, ActionCompletion completion) const {
    if (!validMikanFeedUrl(url)) { completion({false, "invalid_mikan_feed_url"}); return; }
    if (!configured()) { completion({false, "qb_unconfigured"}); return; }
    auto client = client_;
    const auto base = baseUrl_;
    const auto form = "username=" + formEncode(username_) + "&password=" + formEncode(password_);
    auto preflight = drogon::HttpRequest::newHttpRequest();
    preflight->setMethod(drogon::Get);
    preflight->setPath("/api/v2/app/version");
    preflight->addHeader("Host", "[::1]:8080");
    client->sendRequest(preflight, [client, base, form, url = std::move(url),
                                    completion = std::move(completion)](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (result != drogon::ReqResult::Ok || !response ||
            (response->statusCode() != drogon::k403Forbidden && response->statusCode() != drogon::k200OK)) {
            completion({false, "qb_unexpected_service"}); return;
        }
        auto login = drogon::HttpRequest::newHttpRequest();
        login->setMethod(drogon::Post);
        login->setPath("/api/v2/auth/login");
        login->addHeader("Host", "[::1]:8080");
        login->addHeader("Content-Type", "application/x-www-form-urlencoded");
        login->addHeader("Referer", base + "/");
        login->setBody(form);
        client->sendRequest(login, [client, base, url = std::move(url),
                                    completion = std::move(completion)](
            drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
            if (result != drogon::ReqResult::Ok || !response) {
                completion({false, "qb_unreachable"}); return;
            }
            const auto cookie = loginCookie(response);
            if (!cookie) { completion({false, "qb_auth_failed"}); return; }
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setMethod(drogon::Post);
            request->setPath("/api/v2/rss/addFeed");
            request->addHeader("Host", "[::1]:8080");
            request->addHeader("Content-Type", "application/x-www-form-urlencoded");
            request->addHeader("Cookie", *cookie);
            request->addHeader("Referer", base + "/");
            request->setBody(mikanFeedForm(url));
            // Only an explicit UI action can add a feed. qB performs the fetch through its own proxy bridge.
            client->sendRequest(request, [completion = std::move(completion)](
                drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
                if (result != drogon::ReqResult::Ok || !response) {
                    completion({false, "qb_unreachable"}); return;
                }
                if (response->statusCode() == drogon::k200OK) completion({true, ""});
                else if (response->statusCode() == drogon::k409Conflict)
                    completion({false, "qb_feed_exists_or_invalid"});
                else completion({false, "qb_api_unavailable"});
            }, 10.0);
        }, 5.0);
    }, 5.0);
}

std::string QbWebClient::mikanFeedForm(const std::string& url) {
    const auto encoded = formEncode(url);
    // qB 5.2 requires path even when it uses the feed URL as the default path.
    return "url=" + encoded + "&path=" + encoded;
}

void QbWebClient::addMikanFeedWithAutoRule(std::string url, std::string savePath,
                                           ActionCompletion completion) const {
    if (!validMikanFeedUrl(url)) { completion({false, "invalid_mikan_feed_url"}); return; }
    if (!configured()) { completion({false, "qb_unconfigured"}); return; }
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char ch : url) { hash ^= ch; hash *= 1099511628211ull; }
    std::ostringstream name;
    name << "AnimeVault Auto " << std::hex << std::setw(16) << std::setfill('0') << hash;
    const QbMikanRuleSpec spec{name.str(), url, "", std::move(savePath)};
    std::string ruleJson;
    std::string pendingJson;
    try {
        ruleJson = autoMikanRuleJson(spec);
        pendingJson = autoMikanRuleJson(spec, false);
    }
    catch (...) { completion({false, "invalid_mikan_rule"}); return; }
    auto client = client_;
    const auto base = baseUrl_;
    const auto form = "username=" + formEncode(username_) + "&password=" + formEncode(password_);
    addMikanFeed(url, [client, base, form, spec, ruleJson = std::move(ruleJson),
                       pendingJson = std::move(pendingJson),
                       completion = std::move(completion)](QbActionResult added) mutable {
        if (!added.success) { completion(std::move(added)); return; }
        std::thread([client, base, form, spec, ruleJson = std::move(ruleJson),
                     pendingJson = std::move(pendingJson),
                     completion = std::move(completion)]() mutable {
            try {
                const auto send = [&](drogon::HttpMethod method, const std::string& path,
                                      const std::string& body, const std::string& cookie) {
                    auto request = drogon::HttpRequest::newHttpRequest();
                    request->setMethod(method);
                    request->setPath(path);
                    request->addHeader("Host", "[::1]:8080");
                    request->addHeader("Referer", base + "/");
                    if (!cookie.empty()) request->addHeader("Cookie", cookie);
                    if (method == drogon::Post) {
                        request->addHeader("Content-Type", "application/x-www-form-urlencoded");
                        request->setBody(body);
                    }
                    const auto [result, response] = client->sendRequest(request, 10.0);
                    return result == drogon::ReqResult::Ok ? response : drogon::HttpResponsePtr{};
                };
                const auto login = send(drogon::Post, "/api/v2/auth/login", form, "");
                const auto cookie = loginCookie(login);
                if (!cookie) { completion({false, "qb_auth_failed"}); return; }
                const auto rulesResponse = send(drogon::Get, "/api/v2/rss/rules", "", *cookie);
                if (!rulesResponse || rulesResponse->statusCode() != drogon::k200OK ||
                    rulesResponse->getBody().size() > 1024 * 1024) {
                    completion({false, "qb_api_unavailable"}); return;
                }
                const auto rules = nlohmann::json::parse(rulesResponse->getBody());
                if (!rules.is_object()) throw std::invalid_argument("invalid qB rules");
                if (rules.contains(spec.ruleName)) {
                    completion({false, "qb_rule_exists"}); return;
                }
                std::optional<std::string> feedPath;
                for (int attempt = 0; attempt < 30 && !feedPath; ++attempt) {
                    const auto items = send(drogon::Get, "/api/v2/rss/items?withData=true", "", *cookie);
                    if (!items || items->statusCode() != drogon::k200OK ||
                        items->getBody().size() > 4 * 1024 * 1024) {
                        completion({false, "qb_api_unavailable"}); return;
                    }
                    feedPath = readyMikanFeedPath(items->getBody(), spec.feedUrl);
                    if (!feedPath) std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                if (!feedPath) { completion({false, "qb_feed_not_ready"}); return; }
                const auto preferences = send(drogon::Get, "/api/v2/app/preferences", "", *cookie);
                if (!preferences || preferences->statusCode() != drogon::k200OK ||
                    preferences->getBody().size() > 1024 * 1024) {
                    completion({false, "qb_api_unavailable"}); return;
                }
                const auto current = nlohmann::json::parse(preferences->getBody());
                if (!current.is_object() || !current.value("rss_auto_downloading_enabled", false) ||
                    !current.value("rss_processing_enabled", false)) {
                    completion({false, "qb_auto_download_disabled"}); return;
                }
                const auto settings = autoEpisodePreferencesJson(preferences->getBody());
                if (current.value("rss_smart_episode_filters", std::string{}) !=
                        nlohmann::json::parse(settings)["rss_smart_episode_filters"].get<std::string>() ||
                    current.value("rss_download_repack_proper_episodes", true)) {
                    const auto saved = send(drogon::Post, "/api/v2/app/setPreferences",
                        "json=" + formEncode(settings), *cookie);
                    if (!saved || (saved->statusCode() != drogon::k200OK &&
                                   saved->statusCode() != drogon::k204NoContent)) {
                        completion({false, "qb_preferences_rejected"}); return;
                    }
                }
                // Baseline existing articles before enabling the new download rule.
                const auto marked = send(drogon::Post, "/api/v2/rss/markAsRead",
                    "itemPath=" + formEncode(*feedPath), *cookie);
                if (!marked || marked->statusCode() != drogon::k200OK) {
                    completion({false, "qb_mark_read_failed"}); return;
                }
                // Inserting a disabled rule resets qB's queued old articles before activation.
                const auto pending = send(drogon::Post, "/api/v2/rss/setRule",
                    "ruleName=" + formEncode(spec.ruleName) +
                    "&ruleDef=" + formEncode(pendingJson), *cookie);
                if (!pending || pending->statusCode() != drogon::k200OK) {
                    completion({false, "qb_rule_rejected"}); return;
                }
                const auto created = send(drogon::Post, "/api/v2/rss/setRule",
                    "ruleName=" + formEncode(spec.ruleName) +
                    "&ruleDef=" + formEncode(ruleJson), *cookie);
                if (!created || created->statusCode() != drogon::k200OK) {
                    completion({false, "qb_rule_rejected"}); return;
                }
                completion({true, ""});
            } catch (...) { completion({false, "qb_bad_response"}); }
        }).detach();
    });
}

void QbWebClient::createMikanRule(QbMikanRuleSpec spec, ActionCompletion completion) const {
    std::string definition;
    try { definition = mikanRuleJson(spec); }
    catch (...) { completion({false, "invalid_mikan_rule"}); return; }
    if (!configured()) { completion({false, "qb_unconfigured"}); return; }
    auto client = client_;
    const auto base = baseUrl_;
    const auto form = "username=" + formEncode(username_) + "&password=" + formEncode(password_);
    auto preflight = drogon::HttpRequest::newHttpRequest();
    preflight->setMethod(drogon::Get);
    preflight->setPath("/api/v2/app/version");
    preflight->addHeader("Host", "[::1]:8080");
    client->sendRequest(preflight, [client, base, form, spec = std::move(spec),
                                    definition = std::move(definition), completion = std::move(completion)](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (result != drogon::ReqResult::Ok || !response ||
            (response->statusCode() != drogon::k403Forbidden && response->statusCode() != drogon::k200OK)) {
            completion({false, "qb_unexpected_service"}); return;
        }
        auto login = drogon::HttpRequest::newHttpRequest();
        login->setMethod(drogon::Post);
        login->setPath("/api/v2/auth/login");
        login->addHeader("Host", "[::1]:8080");
        login->addHeader("Content-Type", "application/x-www-form-urlencoded");
        login->addHeader("Referer", base + "/");
        login->setBody(form);
        client->sendRequest(login, [client, base, spec = std::move(spec),
                                    definition = std::move(definition), completion = std::move(completion)](
            drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
            if (result != drogon::ReqResult::Ok || !response) {
                completion({false, "qb_unreachable"}); return;
            }
            const auto cookie = loginCookie(response);
            if (!cookie) { completion({false, "qb_auth_failed"}); return; }
            auto rules = drogon::HttpRequest::newHttpRequest();
            rules->setMethod(drogon::Get);
            rules->setPath("/api/v2/rss/rules");
            rules->addHeader("Host", "[::1]:8080");
            rules->addHeader("Cookie", *cookie);
            rules->addHeader("Referer", base + "/");
            client->sendRequest(rules, [client, base, cookie = *cookie, spec = std::move(spec),
                                        definition = std::move(definition), completion = std::move(completion)](
                drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
                if (result != drogon::ReqResult::Ok || !response ||
                    response->statusCode() != drogon::k200OK || response->getBody().size() > 1024 * 1024) {
                    completion({false, "qb_api_unavailable"}); return;
                }
                try {
                    const auto existing = nlohmann::json::parse(response->getBody());
                    if (!existing.is_object()) throw std::invalid_argument("invalid qB RSS rules");
                    // setRule overwrites a same-named qB rule, so stop before touching user rules.
                    if (existing.contains(spec.ruleName)) {
                        completion({false, "qb_rule_exists"}); return;
                    }
                } catch (...) { completion({false, "qb_bad_response"}); return; }
                auto setRule = drogon::HttpRequest::newHttpRequest();
                setRule->setMethod(drogon::Post);
                setRule->setPath("/api/v2/rss/setRule");
                setRule->addHeader("Host", "[::1]:8080");
                setRule->addHeader("Content-Type", "application/x-www-form-urlencoded");
                setRule->addHeader("Cookie", cookie);
                setRule->addHeader("Referer", base + "/");
                setRule->setBody("ruleName=" + formEncode(spec.ruleName) +
                                 "&ruleDef=" + formEncode(definition));
                client->sendRequest(setRule, [completion = std::move(completion)](
                    drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
                    if (result != drogon::ReqResult::Ok || !response) {
                        completion({false, "qb_unreachable"}); return;
                    }
                    if (response->statusCode() == drogon::k200OK) completion({true, ""});
                    else completion({false, "qb_rule_rejected"});
                }, 10.0);
            }, 5.0);
        }, 5.0);
    }, 5.0);
}
}
