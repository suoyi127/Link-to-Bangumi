#include "anime_vault/infrastructure/network/QbWebClient.hpp"
#include "anime_vault/services/TitleNormalization.hpp"
#include "anime_vault/services/FilenameParser.hpp"
#include "anime_vault/services/ReleaseTextNormalization.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <set>
#include <cctype>
#include <charconv>
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
    return R"((?<=\[)0*(\d{1,3})(?:v\d+)?(?=\]))";
}

std::string QbWebClient::dashedEpisodePattern() {
    // qB 使用扩展正则模式，字面空格必须转义。
    return R"((?<=-\x20)0*(\d{1,3})(?:v\d+)?(?=\s*(?:\[|\(|-)))";
}

std::string QbWebClient::autoMikanRuleJson(const QbMikanRuleSpec& spec, bool enabled) {
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

std::string QbWebClient::autoEpisodePreferencesJson(std::string_view currentPreferences) {
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

std::string QbWebClient::autoMikanHistoryJson(const QbMikanRuleSpec& spec, std::string_view rulesText,
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
