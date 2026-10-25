#include "anime_vault/infrastructure/network/QbWebClient.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>
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
std::string hostHeader(std::string_view base) { return std::string(base.substr(7)); }
bool verifiedLandingPage(drogon::ReqResult result, const drogon::HttpResponsePtr& response) {
    return result == drogon::ReqResult::Ok && response &&
        response->statusCode() == drogon::k200OK &&
        QbWebClient::looksLikeQbWebUi(response->getBody());
}
}

QbEndpoint QbWebClient::parseEndpoint(std::string_view baseUrl) {
    if (!baseUrl.starts_with("http://")) throw std::invalid_argument("qB URL must use local HTTP");
    const auto authority = baseUrl.substr(7);
    std::string_view address;
    std::string_view portText;
    if (authority.starts_with("[::1]:")) {
        address = "::1";
        portText = authority.substr(6);
    } else if (authority.starts_with("127.0.0.1:")) {
        address = "127.0.0.1";
        portText = authority.substr(10);
    } else {
        throw std::invalid_argument("qB URL must use an explicit loopback address");
    }
    int port{};
    const auto [end, error] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
    if (portText.empty() || error != std::errc{} || end != portText.data() + portText.size() ||
        port < 1 || port > 65535)
        throw std::invalid_argument("invalid qB port");
    return {std::string(address), std::string(authority), port};
}

bool QbWebClient::looksLikeQbWebUi(std::string_view body) {
    return body.size() <= 1024 * 1024 && body.find("qBittorrent WebUI") != std::string_view::npos;
}

QbWebClient::QbWebClient(std::string baseUrl, std::string username, std::string password)
    : baseUrl_(std::move(baseUrl)), username_(std::move(username)), password_(std::move(password)) {
    const auto endpoint = parseEndpoint(baseUrl_);
    // A numeric address avoids DNS changing which local service receives the credentials.
    if (configured()) client_ = drogon::HttpClient::newHttpClient(
        endpoint.address, static_cast<std::uint16_t>(endpoint.port));
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
    return QbRssRules::parseMikanItems(body);
}
bool QbWebClient::validMikanFeedUrl(std::string_view url) {
    return QbRssRules::validMikanFeedUrl(url);
}
std::string QbWebClient::mikanRuleJson(const QbMikanRuleSpec& spec) {
    return QbRssRules::mikanRuleJson(spec);
}
std::string QbWebClient::bracketEpisodePattern() { return QbRssRules::bracketEpisodePattern(); }
std::string QbWebClient::dashedEpisodePattern() { return QbRssRules::dashedEpisodePattern(); }
std::string QbWebClient::autoMikanRuleJson(const QbMikanRuleSpec& spec, bool enabled) {
    return QbRssRules::autoMikanRuleJson(spec, enabled);
}
std::string QbWebClient::autoEpisodePreferencesJson(std::string_view currentPreferences) {
    return QbRssRules::autoEpisodePreferencesJson(currentPreferences);
}
std::string QbWebClient::autoMikanHistoryJson(const QbMikanRuleSpec& spec, std::string_view rules,
                                          std::string_view items, std::string_view torrents) {
    return QbRssRules::autoMikanHistoryJson(spec, rules, items, torrents);
}
std::optional<std::string> QbWebClient::readyMikanFeedPath(std::string_view items, std::string_view url) {
    return QbRssRules::readyMikanFeedPath(items, url);
}

bool QbWebClient::configured() const noexcept { return !username_.empty() && !password_.empty(); }

std::vector<std::string> QbWebClient::completedPaths(std::string_view body) {
    if (body.size() > 4 * 1024 * 1024) throw std::invalid_argument("qB response too large");
    const auto data = nlohmann::json::parse(body);
    if (!data.is_array()) throw std::invalid_argument("invalid qB response");
    std::vector<std::string> paths;
    for (const auto& torrent : data) {
        if (!torrent.is_object()) continue;
        const auto state = torrent.value("state", std::string{});
        // 做种只读取源文件；校验、错误和未完成任务不能作为完成凭据。
        if (torrent.value("progress", 0.0) < 1.0 ||
            (state != "uploading" && state != "stalledUP" && state != "pausedUP" &&
             state != "stoppedUP" && state != "queuedUP" && state != "forcedUP")) continue;
        const auto name = torrent.value("content_path", std::string{});
        const auto path = std::filesystem::path(std::u8string(name.begin(), name.end()));
        if (!path.is_absolute() || path == path.root_path()) continue;
        bool traversal = false;
        for (const auto& part : path) if (part == "..") traversal = true;
        if (!traversal) paths.push_back(name);
    }
    return paths;
}

void QbWebClient::inspect(Completion completion) const {
    if (!configured()) { completion({false, false, "qb_unconfigured"}); return; }
    auto preflight = drogon::HttpRequest::newHttpRequest();
    preflight->setMethod(drogon::Get);
    preflight->setPath("/");
    preflight->addHeader("Host", hostHeader(baseUrl_));
    auto login = drogon::HttpRequest::newHttpRequest();
    login->setMethod(drogon::Post);
    login->setPath("/api/v2/auth/login");
    login->addHeader("Content-Type", "application/x-www-form-urlencoded");
    login->addHeader("Host", hostHeader(baseUrl_));
    login->addHeader("Referer", baseUrl_ + "/");
    login->setBody("username=" + formEncode(username_) + "&password=" + formEncode(password_));
    auto client = client_;
    client->sendRequest(preflight, [client, login, completion = std::move(completion), base = baseUrl_](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        // An unrelated IPv4 server also owns port 8080; reject an unexpected endpoint before login.
        if (!verifiedLandingPage(result, response)) {
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
        request->addHeader("Host", hostHeader(base));
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
            torrents->addHeader("Host", hostHeader(base));
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
                    status.completedPaths = completedPaths(response->getBody());
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
    preflight->setPath("/");
    preflight->addHeader("Host", hostHeader(baseUrl_));
    auto client = client_;
    const auto base = baseUrl_;
    const auto form = "username=" + formEncode(username_) + "&password=" + formEncode(password_);
    client->sendRequest(preflight, [client, base, form, completion = std::move(completion)](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (!verifiedLandingPage(result, response)) {
            completion({0, 0, {}, "qb_unexpected_service"}); return;
        }
        auto login = drogon::HttpRequest::newHttpRequest();
        login->setMethod(drogon::Post);
        login->setPath("/api/v2/auth/login");
        login->addHeader("Host", hostHeader(base));
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
            request->addHeader("Host", hostHeader(base));
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
    preflight->setPath("/");
    preflight->addHeader("Host", hostHeader(base));
    client->sendRequest(preflight, [client, base, form, url = std::move(url),
                                    completion = std::move(completion)](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (!verifiedLandingPage(result, response)) {
            completion({false, "qb_unexpected_service"}); return;
        }
        auto login = drogon::HttpRequest::newHttpRequest();
        login->setMethod(drogon::Post);
        login->setPath("/api/v2/auth/login");
        login->addHeader("Host", hostHeader(base));
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
            request->addHeader("Host", hostHeader(base));
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
    return QbRssRules::mikanFeedForm(url);
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
                    request->addHeader("Host", hostHeader(base));
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
                for (const auto& rule : rules) {
                    if (!rule.is_object() || !rule.value("enabled", false)) continue;
                    const auto feeds = rule.value("affectedFeeds", nlohmann::json::array());
                    if (std::find(feeds.begin(), feeds.end(), spec.feedUrl) != feeds.end()) {
                        completion({false, "qb_rule_exists"}); return;
                    }
                }
                std::optional<std::string> feedPath;
                std::string feedItems;
                for (int attempt = 0; attempt < 30 && !feedPath; ++attempt) {
                    const auto items = send(drogon::Get, "/api/v2/rss/items?withData=true", "", *cookie);
                    if (!items || items->statusCode() != drogon::k200OK ||
                        items->getBody().size() > 4 * 1024 * 1024) {
                        completion({false, "qb_api_unavailable"}); return;
                    }
                    feedItems = std::string(items->getBody());
                    feedPath = readyMikanFeedPath(feedItems, spec.feedUrl);
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
                const auto tasks = send(drogon::Get, "/api/v2/torrents/info", "", *cookie);
                if (!tasks || tasks->statusCode() != drogon::k200OK || tasks->getBody().size() > 8 * 1024 * 1024) {
                    completion({false, "qb_api_unavailable"}); return;
                }
                const auto history = nlohmann::json::parse(autoMikanHistoryJson(
                    spec, rulesResponse->getBody(), feedItems, tasks->getBody()));
                auto activeRule = nlohmann::json::parse(ruleJson);
                activeRule["previouslyMatchedEpisodes"] = history;
                ruleJson = activeRule.dump();
                activeRule["enabled"] = false;
                pendingJson = activeRule.dump();
                // 先创建禁用规则，再启用；已读状态不作为下载完成的证据。
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
    preflight->setPath("/");
    preflight->addHeader("Host", hostHeader(base));
    client->sendRequest(preflight, [client, base, form, spec = std::move(spec),
                                    definition = std::move(definition), completion = std::move(completion)](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (!verifiedLandingPage(result, response)) {
            completion({false, "qb_unexpected_service"}); return;
        }
        auto login = drogon::HttpRequest::newHttpRequest();
        login->setMethod(drogon::Post);
        login->setPath("/api/v2/auth/login");
        login->addHeader("Host", hostHeader(base));
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
            rules->addHeader("Host", hostHeader(base));
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
                setRule->addHeader("Host", hostHeader(base));
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
