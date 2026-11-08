#include "anime_vault/infrastructure/network/MikanDownloadWorker.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <drogon/utils/Utilities.h>
#include <sstream>
#include <regex>
#include "anime_vault/services/MikanChinese.hpp"
#include "anime_vault/services/MikanSubjectLink.hpp"

namespace anime_vault {
namespace {
using Json = nlohmann::json;
using Request = QbWebClient::MikanRequest;
std::string encode(const std::string &text) {
    const char *hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : text) {
        if (isalnum(c) && c < 128)
            result.push_back(static_cast<char>(c));
        else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
std::string read(const Request &send, const std::string &path) {
    auto [status, body] = send(path, "");
    if (status != 200)
        throw QbConfigError("qb_api_unavailable", "qB RSS read failed");
    return body;
}
const Json *findFeedNode(const Json &node, const std::string &url, int depth = 0) {
    if (depth > 10 || !node.is_object())
        return nullptr;
    if (node.contains("url"))
        return node.value("url", "") == url ? &node : nullptr;
    for (const auto &child : node)
        if (const auto *match = findFeedNode(child, url, depth + 1))
            return match;
    return nullptr;
}
bool feedPresent(const Json &node, const std::string &url) { return findFeedNode(node, url) != nullptr; }
std::string oldName(const std::string &url) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : url) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << "AnimeVault Auto " << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}
bool sameSavePath(std::string left, std::string right) {
    std::replace(left.begin(), left.end(), '\\', '/');
    std::replace(right.begin(), right.end(), '\\', '/');
    return left == right;
}
bool legacyTorrentParams(const Json &params, const std::string &path) {
    if (!params.is_object())
        return false;
    const Json defaults = {{"category", ""},
                           {"tags", Json::array()},
                           {"download_path", ""},
                           {"operating_mode", "AutoManaged"},
                           {"skip_checking", false},
                           {"upload_limit", -1},
                           {"download_limit", -1},
                           {"seeding_time_limit", -2},
                           {"inactive_seeding_time_limit", -2},
                           {"ratio_limit", -2},
                           {"share_limit_action", "Default"},
                           {"share_limits_mode", "Default"},
                           {"seed_mode", false},
                           {"ssl_certificate", ""},
                           {"ssl_private_key", ""},
                           {"ssl_dh_params", ""},
                           {"stopped", false},
                           {"use_auto_tmm", false}};
    for (auto field = params.begin(); field != params.end(); ++field) {
        if (field.key() == "save_path") {
            if (!field->is_string() || !sameSavePath(field->get<std::string>(), path))
                return false;
        } else if (!defaults.contains(field.key()) || *field != defaults[field.key()])
            return false;
    }
    return true;
}
bool clearForFeed(const Json &rules, const MikanSubscription &feed, const std::string &path, const Request &send,
                  const std::function<void()> &enroll = {}, bool preview = false) {
    std::string legacyName;
    Json legacy;
    for (auto it = rules.begin(); it != rules.end(); ++it) {
        const auto &rule = it.value();
        if (!rule.value("enabled", false))
            continue;
        const auto urls = rule.value("affectedFeeds", Json::array());
        if (std::find(urls.begin(), urls.end(), feed.url) == urls.end())
            continue;
        if (!enroll || it.key() != oldName(feed.url))
            return false;
        auto expected = Json::parse(QbRssRules::autoMikanRuleJson({it.key(), feed.url, "", path}));
        // 仅迁移应用原样生成的旧规则，用户编辑过的规则需要自行停用。
        // 旧版生成器曾为每个集数表达式单独加括号；只接受已知完整模板。
        static const std::string older =
            R"((?:_|\b)(?:\bs\d{1,2}e0*(\d{1,3})(?:v\d+)?\b)|(?:\b\d{1,2}x0*(\d{1,3})(?:v\d+)?\b)|(?:\bep0*(\d{1,3})(?:v\d+)?\b)|(?:(?<=\[)0*(\d{1,3})(?:v\d+)?(?=\]))|(?:(?<=- )0*(\d{1,3})(?:v\d+)?(?=\s*(?:\[|\(|-)))(?:_|\b))";
        if (rule.value("mustContain", "") == older && rule.value("mustNotContain", "").empty()) {
            expected["mustContain"] = older;
            expected["mustNotContain"] = "";
        }
        if (!sameSavePath(rule.value("savePath", ""), path))
            return false;
        for (const auto *key : {"mustContain", "mustNotContain", "useRegex", "smartFilter", "affectedFeeds",
                                "episodeFilter", "assignedCategory", "ignoreDays", "addPaused"})
            if (!rule.contains(key) || rule[key] != expected[key])
                return false;
        for (auto field = rule.begin(); field != rule.end(); ++field) {
            if (expected.contains(field.key()) || field.key() == "lastMatch")
                continue;
            if (field.key() == "torrentParams") {
                if (!legacyTorrentParams(*field, path))
                    return false;
                continue;
            }
            if (!field.value().empty() && field.value() != false && field.value() != 0 && field.value() != "")
                return false;
        }
        legacyName = it.key();
        legacy = rule;
    }
    if (preview)
        return true;
    if (enroll)
        enroll();
    if (!legacyName.empty()) {
        legacy["enabled"] = false;
        const auto status =
            send("/api/v2/rss/setRule", "ruleName=" + encode(legacyName) + "&ruleDef=" + encode(legacy.dump())).first;
        if (status != 200 && status != 204)
            throw QbConfigError("qb_rule_rejected", "qB rejected legacy rule migration");
    }
    return true;
}
} // namespace
MikanDownloadWorker::MikanDownloadWorker(SqliteDatabase &db, std::shared_ptr<QbConnectionManager> manager,
                                         std::string path, std::function<void()> validate)
    : store_(db), manager_(std::move(manager)), savePath_(std::move(path)), validate_(std::move(validate)),
      thread_([this](std::stop_token token) { run(token); }) {}
MikanDownloadWorker::~MikanDownloadWorker() { stop(); }
void MikanDownloadWorker::stop() {
    thread_.request_stop();
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
void MikanDownloadWorker::subscribe(MikanSubscription feed, QbWebClient::ActionCompletion complete) {
    const auto series = mikanSeriesId(feed.url).value_or(0);
    std::int64_t account{};
    try {
        // 在提交时确定账号，避免等待 qB 的过程中把收藏发给另一个账号。
        if (series)
            account = store_.collectionAccount();
    } catch (const std::exception &) {
        complete({false, "watch_sync_account_unavailable"});
        return;
    }
    std::lock_guard lock(mutex_);
    jobs_.push_back({std::move(feed), std::move(complete), {}, false, series, account});
    cv_.notify_one();
}
void MikanDownloadWorker::synchronize(bool preview, SyncCompletion complete) {
    std::lock_guard lock(mutex_);
    jobs_.push_back({{}, {}, std::move(complete), preview});
    cv_.notify_one();
}
void MikanDownloadWorker::run(std::stop_token token) {
    auto next = std::chrono::steady_clock::now();
    while (!token.stop_requested()) {
        std::optional<Job> job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait_until(lock, token, next, [&] { return !jobs_.empty(); });
            if (token.stop_requested())
                return;
            if (!jobs_.empty()) {
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
        }
        if (job && job->onSync) {
            MikanRuleSync report;
            try {
                syncRules(job->preview, report);
            } catch (const QbConfigError &e) {
                report.errorCode = e.code;
            } catch (...) {
                report.errorCode = "qb_api_unavailable";
            }
            job->onSync(report);
            next = std::chrono::steady_clock::now() + std::chrono::minutes(1);
            continue;
        }
        QbActionResult result{true, ""};
        try {
            refresh(job, token);
        } catch (const QbConfigError &e) {
            result = {false, e.code};
        } catch (...) {
            result = {false, "qb_api_unavailable"};
        }
        if (job)
            job->complete(result);
        else if (!result.success)
            std::cerr << "Mikan RSS: " << result.errorCode << '\n';
        next = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    }
}
void MikanDownloadWorker::syncRules(bool preview, MikanRuleSync &result) {
    auto client = manager_->current();
    if (!client->configured())
        throw QbConfigError("qb_unconfigured", "qB not configured");
    validate_();
    auto send = client->openMikanSession();
    const auto prefs = Json::parse(read(send, "/api/v2/app/preferences"));
    if (!prefs.value("rss_auto_downloading_enabled", false) || !prefs.value("rss_processing_enabled", false))
        throw QbConfigError("qb_auto_download_disabled", "qB RSS disabled");
    auto endpoint = client->baseUrl();
    const auto existing = store_.feeds(endpoint);
    const auto rules = Json::parse(read(send, "/api/v2/rss/rules"));
    const auto items = read(send, "/api/v2/rss/items?withData=true");
    const auto tree = Json::parse(items);
    if (!rules.is_object() || !tree.is_object())
        throw QbConfigError("qb_bad_response", "invalid qB data");

    for (auto rule = rules.begin(); rule != rules.end(); ++rule) {
        const bool enabled = rule->value("enabled", false);
        auto affected = rule->value("affectedFeeds", Json::array());
        if (!affected.is_array() || affected.size() != 1 || !affected.front().is_string()) {
            if (enabled)
                ++result.retained;
            continue;
        }
        const auto feed = affected.front().get<std::string>();
        if (!isMikanSingleSeriesFeed(feed) || rule.key() != oldName(feed)) {
            if (enabled)
                ++result.retained;
            continue;
        }
        if (!enabled) {
            if (std::any_of(existing.begin(), existing.end(), [&](const auto &f) { return f.url == feed; }))
                ++result.alreadyManaged;
            continue;
        }
        MikanSubscription subscription{feed};
        if (!clearForFeed(rules, subscription, savePath_, send, [] {}, true)) {
            ++result.retained;
            continue;
        }
        const auto *node = findFeedNode(tree, feed);
        if (!node || node->value("isLoading", false) || node->value("hasError", false) || !node->contains("articles") ||
            !(*node)["articles"].is_array() || (*node)["articles"].empty()) {
            ++result.unavailable;
            continue;
        }
        MikanAliases aliases;
        auto candidates = selectMikanDownloads({feed}, items, "[]", &aliases);
        std::set<std::string> urls;
        for (const auto &article : (*node)["articles"])
            if (article.is_object()) {
                auto url = article.value("torrentURL", "");
                if (!url.empty())
                    urls.insert(url);
            }
        std::map<std::string, std::set<std::string>> history;
        bool supported = true;
        auto previous = rule->value("previouslyMatchedEpisodes", Json::array());
        if (!previous.is_array())
            supported = false;
        for (const auto &ep : previous) {
            if (!ep.is_string()) {
                supported = false;
                break;
            }
            auto n = ep.get<std::string>();
            if (n.empty() || n.size() > 3 || n.find_first_not_of("0123456789") != std::string::npos) {
                supported = false;
                break;
            }
            history[std::to_string(std::stoi(n))].insert("");
        }
        if (!supported) {
            ++result.retained;
            continue;
        }
        for (const auto &c : candidates) {
            const auto identity = resolveMikanIdentity(c.identity, aliases);
            auto parts = Json::parse(identity);
            auto ep = history.find(parts.at(3).get<std::string>());
            if (parts.at(2) == 0 && ep != history.end())
                ep->second.insert(identity);
        }
        ++result.eligible;
        result.baselineArticles += static_cast<int>(urls.size());
        result.historicalEpisodes += static_cast<int>(history.size());
        result.aliases += static_cast<int>(aliases.size());
        if (preview)
            continue;
        // 先持久保存接管时的 RSS 快照、集数历史和偏好，再停用经过核验的旧规则。
        const auto currentRules = Json::parse(read(send, "/api/v2/rss/rules"));
        if (!currentRules.contains(rule.key()) || currentRules.at(rule.key()) != *rule ||
            !clearForFeed(currentRules, subscription, savePath_, send,
                          [&] { store_.adoptRule(endpoint, feed, urls, history, aliases); })) {
            ++result.unavailable;
            continue;
        }
        ++result.migrated;
    }
}
void MikanDownloadWorker::refresh(const std::optional<Job> &job, std::stop_token token) {
    auto client = manager_->current();
    const auto endpoint = client->baseUrl();
    auto feeds = store_.feeds(endpoint);
    if (!job && feeds.empty())
        return;
    if (!client->configured())
        throw QbConfigError("qb_unconfigured", "qB not configured");
    validate_();
    const auto send = client->openMikanSession();
    const auto prefs = Json::parse(read(send, "/api/v2/app/preferences"));
    if (!prefs.value("rss_auto_downloading_enabled", false) || !prefs.value("rss_processing_enabled", false))
        throw QbConfigError("qb_auto_download_disabled", "qB RSS disabled");
    const auto rules = Json::parse(read(send, "/api/v2/rss/rules"));
    if (!rules.is_object())
        throw QbConfigError("qb_bad_response", "invalid qB rules");
    auto items = read(send, "/api/v2/rss/items?withData=true");
    if (job) {
        const auto &feed = job->feed;
        if (!feedPresent(Json::parse(items), feed.url)) {
            const auto status = send("/api/v2/rss/addFeed", QbRssRules::mikanFeedForm(feed.url)).first;
            if (status != 200 && status != 204)
                throw QbConfigError("qb_feed_exists_or_invalid", "qB RSS add failed");
        }
        if (!clearForFeed(rules, feed, savePath_, send,
                          [&] { store_.subscribe(endpoint, feed, job->seriesId, job->account); }))
            throw QbConfigError("qb_rule_exists", "qB rule conflict");
        if (job->seriesId)
            store_.activateCollection(endpoint, feed.url);
        feeds = store_.feeds(endpoint);
    }
    const auto tasks = read(send, "/api/v2/torrents/info");
    const auto tree = Json::parse(items);
    const auto taskList = Json::parse(tasks);
    const auto tagFor = [](const MikanCandidate &c) {
        return "AnimeVault-" + drogon::utils::getSha256(c.url).substr(0, 24);
    };
    auto submit = [&](const MikanCandidate &candidate) {
        store_.retry(endpoint, candidate, 180);
        auto [status, body] =
            send("/api/v2/torrents/add", "urls=" + encode(candidate.url) + "&savepath=" + encode(savePath_) +
                                             "&tags=" + encode(tagFor(candidate)));
        const auto reply = Json::parse(body, nullptr, false);
        const bool failed =
            status != 200 || (reply.is_object() ? reply.value("success_count", 0) + reply.value("pending_count", 0) == 0
                                                : (!body.empty() && body != "Ok."));
        if (failed) {
            store_.retry(endpoint, candidate, 0);
            if (job)
                throw QbConfigError("qb_torrent_rejected", "qB rejected torrent");
            std::cerr << "Mikan RSS: qb_torrent_rejected\n";
        }
    };
    auto known = store_.aliases(endpoint, "");
    const auto initialAliases = known;
    std::vector<std::vector<MikanCandidate>> batch(feeds.size());
    std::vector<bool> active(feeds.size());
    std::vector<std::set<std::string>> baseline(feeds.size());
    for (std::size_t i = 0; i < feeds.size(); ++i) {
        const auto &feed = feeds[i];
        active[i] = feedPresent(tree, feed.url) &&
                    ((job && job->feed.url == feed.url) || clearForFeed(rules, feed, savePath_, send));
        if (!active[i])
            continue;
        // 崩溃或旧 qB 规则停用失败后，仅在该订阅实际可用时恢复收藏任务。
        if (!job || job->feed.url!=feed.url) store_.activateCollection(endpoint,feed.url);
        baseline[i] = store_.baseline(endpoint, feed.url);
        const auto before = known;
        std::map<std::string, int> observed;
        batch[i] = selectMikanDownloads(feed, items, tasks, &known, &observed, &baseline[i]);
        store_.learnSubtitleScripts(endpoint, feed.url, observed);
        MikanAliases added;
        for (const auto &[alias, owners] : known) {
            auto old = before.find(alias);
            for (const auto &owner : owners)
                if (old == before.end() || !old->second.contains(owner))
                    added[alias].insert(owner);
        }
        if (!added.empty())
            store_.saveAliases(endpoint, feed.url, added);
    }
    using Pick = std::pair<std::size_t, MikanCandidate>;
    std::map<std::string, Pick> winners;
    for (std::size_t i = 0; i < feeds.size(); ++i) {
        if (!active[i])
            continue;
        if (initialAliases != known)
            batch[i] = selectMikanDownloads(feeds[i], items, tasks, &known, nullptr, &baseline[i]);
        for (auto &candidate : batch[i]) {
            const auto rank = [&](std::size_t index, const MikanCandidate &release) {
                const bool ranked = feeds[index].hasPreferences();
                return std::tuple{release.score, ranked ? release.revision : 0, release.downloaded};
            };
            auto [it, inserted] = winners.try_emplace(candidate.identity, i, candidate);
            if (!inserted && rank(i, candidate) > rank(it->second.first, it->second.second))
                it->second = {i, candidate};
        }
    }
    // 待抓取的 URL 保留到 qB 出现任务。到期后允许新候选接替失效链接，
    // 已移除订阅的占位也不能阻挡其他仍启用的订阅。
    for (std::size_t i = 0; i < feeds.size(); ++i) {
        const auto &feed = feeds[i];
        for (const auto &[pending, due] : store_.pending(endpoint, feed)) {
            if (token.stop_requested())
                return;
            const auto tag = tagFor(pending);
            const bool found = std::any_of(taskList.begin(), taskList.end(), [&](const auto &task) {
                return task.value("tags", "").find(tag) != std::string::npos || task.value("name", "") == pending.title;
            });
            if (found) {
                store_.record(endpoint, feed, pending);
                continue;
            }
            const auto key = resolveMikanIdentity(pending.identity, known);
            auto next = winners.find(key);
            if (next != winners.end() && next->second.second.downloaded) {
                store_.record(endpoint, feeds[next->second.first], next->second.second);
                store_.forget(endpoint, pending.url);
                winners.erase(next);
            } else if (!due) {
                if (next != winners.end())
                    winners.erase(next);
            } else if (!active[i] || (next != winners.end() && next->second.second.url != pending.url)) {
                store_.forget(endpoint, pending.url);
            } else {
                if (next != winners.end())
                    winners.erase(next);
                if (store_.wanted(endpoint, feed, pending))
                    submit(pending);
                else
                    store_.forget(endpoint, pending.url);
            }
        }
    }
    for (const auto &[key, pick] : winners) {
        if (token.stop_requested())
            return;
        const auto &[index, candidate] = pick;
        const auto &feed = feeds[index];
        if (!store_.wanted(endpoint, feed, candidate))
            continue;
        if (candidate.downloaded)
            store_.record(endpoint, feed, candidate);
        else if (store_.reserve(endpoint, feed, candidate))
            submit(candidate);
    }
}
} // namespace anime_vault
