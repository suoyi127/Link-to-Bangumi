#include "anime_vault/services/WatchSyncService.hpp"
#include "anime_vault/services/MikanSubjectLink.hpp"
#include "anime_vault/domain/EpisodeNumber.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "../infrastructure/database/SqliteSupport.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace anime_vault {
namespace {
using namespace sqlite_support;
using Id = std::int64_t;
using Json = nlohmann::json;

void parameter(sqlite3_stmt* s, int i, Id value) {
    if (sqlite3_bind_int64(s, i, value) != SQLITE_OK)
        throw std::runtime_error("sqlite bind failed");
}
void parameter(sqlite3_stmt* s, int i, const std::string& value) { sqlite_support::bind(s, i, value); }
template <class... Args> auto query(SqliteDatabase& db, const char* sql, const Args&... args) {
    auto s = prepare(db.handle(), sql);
    [[maybe_unused]] int i = 0;
    (parameter(s.get(), ++i, args), ...);
    return s;
}
template <class... Args> void update(SqliteDatabase& db, const char* sql, const Args&... args) {
    auto s = query(db, sql, args...);
    done(db.handle(), s.get());
}
bool row(sqlite3_stmt* s) {
    auto rc = sqlite3_step(s);
    if (rc == SQLITE_ROW)
        return true;
    if (rc == SQLITE_DONE)
        return false;
    throw std::runtime_error("watch database read failed");
}
Id integer(sqlite3_stmt* s, int i) { return sqlite3_column_int64(s, i); }
Id accountId(SqliteDatabase& db) {
    std::lock_guard lock(db.mutex());
    auto q = query(db, "SELECT account FROM watch_sync_account");
    row(q.get());
    return integer(q.get(), 0);
}
void check(const WatchResponse& r, int expected) {
    if (r.status == expected)
        return;
    if (r.status == 401)
        throw WatchSyncError(401, "bangumi_unauthorized");
    if (r.status == 403)
        throw WatchSyncError(403, "bangumi_forbidden");
    if (r.status == 429)
        throw WatchSyncError(429, "bangumi_rate_limited");
    throw WatchSyncError(502, r.status == 0 ? "bangumi_network_error" : "bangumi_http_error");
}
Json document(const WatchResponse& r) {
    check(r, 200);
    auto result = Json::parse(r.body, nullptr, false);
    if (!result.is_object())
        throw WatchSyncError(502, "bangumi_invalid_response");
    return result;
}
Id jsonId(const Json& doc, const char* key) {
    if (!doc.contains(key) || !doc.at(key).is_number_integer())
        throw WatchSyncError(502, "bangumi_invalid_response");
    return doc.at(key).get<Id>();
}
void requireToken(const std::string& token) {
    if (token.empty() || token.size() > 2048 ||
        token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~+/=") !=
            std::string::npos)
        throw WatchSyncError(400, "invalid_bangumi_token");
}

struct Target {
    Id queue{}, media{}, subject{}, account{}, cached{}, blocked{};
    std::string type, number, raw, source, username;
    bool operator==(const Target&) const = default;
};
std::optional<Target> target(SqliteDatabase& db, Id media) {
    std::lock_guard lock(db.mutex());
    auto q =
        query(db,
              "SELECT q.id,q.media_id,a.bangumi_subject_id,q.account,c.episode_id,q.blocked_subject,"
              "m.episode_type,m.episode_number,m.raw_episode_number,m.episode_number_source,s.username "
              "FROM watch_sync_queue q JOIN watch_sync_account s ON s.account=q.account AND s.enabled AND s.connected "
              "JOIN watch_progress p ON p.media_id=q.media_id AND p.watched "
              "JOIN media_file m ON m.id=q.media_id LEFT JOIN anime a ON a.id=m.anime_id "
              "LEFT JOIN watch_bangumi_episode c ON c.media_id=m.id AND c.subject=a.bangumi_subject_id "
              "AND c.episode_type=m.episode_type AND c.number=m.episode_number AND c.raw_number=m.raw_episode_number "
              "AND c.source=m.episode_number_source "
              "WHERE ((?=0 AND q.retry_at<=unixepoch()) OR q.media_id=?) ORDER BY q.retry_at,q.id LIMIT 1",
              media, media);
    auto s = q.get();
    if (!row(s))
        return {};
    return Target{integer(s, 0), integer(s, 1), integer(s, 2), integer(s, 3), integer(s, 4), integer(s, 5),
                  column(s, 6),  column(s, 7),  column(s, 8),  column(s, 9),  column(s, 10)};
}
int episodeType(const std::string& type) {
    if (type == "normal")
        return 0;
    if (type == "sp")
        return 1;
    if (type == "ncop")
        return 2;
    if (type == "nced")
        return 3;
    throw WatchSyncError(409, "episode_unmapped");
}
double number(const std::string& text) {
    auto parsed = EpisodeNumber::parse(text);
    if (!parsed)
        throw WatchSyncError(409, "episode_unmapped");
    return (10.0 * parsed->whole() + parsed->tenth().value_or(0)) / 10.0;
}
bool matches(const Json& item, const Target& t, int type) {
    if (jsonId(item, "type") != type)
        return false;
    if (item.contains("subject_id") && jsonId(item, "subject_id") != t.subject)
        throw WatchSyncError(502, "bangumi_invalid_response");
    if (!item.contains("sort") || !item["sort"].is_number())
        throw WatchSyncError(502, "bangumi_invalid_response");
    const auto sort = item["sort"].get<double>();
    const auto local = number(t.number);
    if (type != 0)
        return sort == local;
    const bool hasEp = item.contains("ep") && item["ep"].is_number() && item["ep"].get<double>() > 0;
    const auto ep = hasEp ? item["ep"].get<double>() : sort;
    // 已换算的季内集数必须同时吻合原始累计序号。未换算的数字可能对应两种编号。
    if (t.source == "bangumi")
        return hasEp && sort == number(t.raw) && ep == local;
    if (t.source == "manual")
        return ep == local;
    return ep == local || sort == local;
}
Id resolve(const WatchSyncService::Request& request, const Target& t, const std::string& token) {
    if (t.subject <= 0)
        throw WatchSyncError(409, "subject_unlinked");
    if (t.cached > 0)
        return t.cached;
    const int type = episodeType(t.type);
    Id candidate = 0, total = -1;
    for (Id offset = 0; total < 0 || offset < total;) {
        auto page =
            document(request("GET",
                             "/v0/episodes?subject_id=" + std::to_string(t.subject) + "&type=" + std::to_string(type) +
                                 "&limit=100&offset=" + std::to_string(offset),
                             "", token));
        auto count = jsonId(page, "total");
        if (count < 0 || count > 10000 || (total >= 0 && count != total) || !page.contains("data") ||
            !page["data"].is_array())
            throw WatchSyncError(502, "bangumi_invalid_response");
        total = count;
        const auto& entries = page["data"];
        if (entries.size() > 100 || offset + static_cast<Id>(entries.size()) > total ||
            (entries.empty() && offset < total))
            throw WatchSyncError(502, "bangumi_invalid_response");
        for (const auto& item : entries) {
            if (!matches(item, t, type))
                continue;
            const auto id = jsonId(item, "id");
            if (id <= 0)
                throw WatchSyncError(502, "bangumi_invalid_response");
            if (candidate)
                throw WatchSyncError(409, "episode_ambiguous");
            candidate = id;
        }
        offset += static_cast<Id>(entries.size());
    }
    if (!candidate)
        throw WatchSyncError(409, "episode_unmapped");
    return candidate;
}
std::pair<Id, std::string> verifiedAccount(const WatchSyncService::Request& request, const std::string& token) {
    auto me = document(request("GET", "/v0/me", "", token));
    const auto id = jsonId(me, "id");
    if (id <= 0 || !me.contains("username") || !me["username"].is_string())
        throw WatchSyncError(502, "bangumi_invalid_response");
    auto username = me["username"].get<std::string>();
    if (username.empty() || username.size() > 100 ||
        username.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string::npos)
        throw WatchSyncError(502, "bangumi_invalid_response");
    return {id, std::move(username)};
}
bool collected(const WatchSyncService::Request& request, Id subject, const std::string& username,
               const std::string& token) {
    auto response = request("GET", "/v0/users/" + username + "/collections/" + std::to_string(subject), "", token);
    if (response.status == 404)
        return false;
    auto data = document(response);
    if (jsonId(data, "subject_id") != subject || jsonId(data, "type") < 1 || jsonId(data, "type") > 5)
        throw WatchSyncError(502, "bangumi_invalid_response");
    return true;
}
void recordFailure(SqliteDatabase& db, const Target& t, const WatchSyncError& e) {
    std::lock_guard lock(db.mutex());
    update(db,
           "UPDATE watch_sync_queue SET error=?,attempts=attempts+1,blocked_subject=?,"
           "retry_at=unixepoch()+MIN(3600,60*(1 << MIN(attempts,6))) WHERE id=?",
           std::string(e.what()), t.subject, t.queue);
    if (e.status == 401 || std::string_view(e.what()) == "credential_missing")
        update(db, "UPDATE watch_sync_account SET connected=0,error=? WHERE account=?", std::string(e.what()),
               t.account);
}
} // namespace

WatchSyncService::WatchSyncService(SqliteDatabase& db, Request request, Read read, Save save, SeriesPage seriesPage)
    : db_(db), seriesPage_(std::move(seriesPage)), request_(std::move(request)), read_(std::move(read)),
      save_(std::move(save)) {}
WatchSyncService::~WatchSyncService() { stop(); }

WatchSyncStatus WatchSyncService::status() const {
    std::lock_guard lock(db_.mutex());
    auto s = query(db_, "SELECT enabled,connected,username,error,account FROM watch_sync_account");
    row(s.get());
    WatchSyncStatus result;
    result.enabled = integer(s.get(), 0) != 0;
    result.connected = integer(s.get(), 1) != 0;
    result.username = column(s.get(), 2);
    result.errorCode = column(s.get(), 3);
    const auto account = integer(s.get(), 4);
    auto count = query(db_, "SELECT COUNT(*) FROM watch_sync_queue WHERE account=?", account);
    row(count.get());
    result.pending = integer(count.get(), 0);
    auto issues =
        query(db_,
              "SELECT q.media_id,q.error,COALESCE(a.display_title,m.filename),m.episode_number,m.episode_type "
              "FROM watch_sync_queue q JOIN media_file m ON q.media_id=m.id LEFT JOIN anime a ON m.anime_id=a.id "
              "WHERE account=? AND error!='' ORDER BY q.id LIMIT 200",
              account);
    while (row(issues.get())) {
        auto v = issues.get();
        result.issues.push_back({integer(v, 0), column(v, 1), column(v, 2), column(v, 3), column(v, 4)});
    }
    auto subs = query(db_, "SELECT count(*) FROM subscription_collection WHERE account=? AND completed=0", account);
    row(subs.get());
    result.subscriptionPending = integer(subs.get(), 0);
    auto errors = query(db_,
                        "SELECT mikan_id,error FROM subscription_collection WHERE account=? AND completed=0 AND "
                        "error<>'' ORDER BY retry_at,mikan_id LIMIT 200",
                        account);
    while (row(errors.get()))
        result.subscriptionIssues.push_back({integer(errors.get(), 0), column(errors.get(), 1)});
    if (result.errorCode.empty() && !result.issues.empty())
        result.errorCode = result.issues.front().errorCode;
    return result;
}

WatchSyncStatus WatchSyncService::configure(const WatchSyncSettings& settings) {
    std::lock_guard operation(operationMutex_);
    const Id previous = accountId(db_);
    if (!settings.enabled) {
        if (settings.token && !settings.token->empty())
            throw WatchSyncError(400, "invalid_request");
        if (previous)
            save_(previous, "");
        std::lock_guard lock(db_.mutex());
        update(db_, "UPDATE watch_sync_account SET enabled=0,connected=0,username='',account=0,error=''");
        return status();
    }
    auto token = settings.token ? *settings.token : previous ? read_(previous) : "";
    requireToken(token);
    const auto [id, username] = verifiedAccount(request_, token);
    save_(id, token);
    if (previous && previous != id)
        save_(previous, "");
    std::lock_guard lock(db_.mutex());
    sqlite_support::Transaction tx(db_.handle());
    update(db_, "UPDATE watch_sync_account SET enabled=1,connected=1,username=?,account=?,error=''", username, id);
    update(db_, "UPDATE watch_sync_queue SET retry_at=0 WHERE account=?", id);
    update(db_, "UPDATE subscription_collection SET retry_at=0 WHERE account=? AND completed=0", id);
    tx.commit();
    return status();
}

void WatchSyncService::processSubscription() {
    Id account{}, series{};
    {
        std::lock_guard lock(db_.mutex());
        auto q =
            query(db_, "SELECT c.account,c.mikan_id FROM subscription_collection c JOIN watch_sync_account s "
                       "ON s.account=c.account AND s.enabled=1 AND s.connected=1 WHERE c.ready=1 AND c.completed=0 "
                       "AND c.retry_at<=unixepoch() ORDER BY c.retry_at,c.mikan_id LIMIT 1");
        if (!row(q.get()))
            return;
        account = integer(q.get(), 0);
        series = integer(q.get(), 1);
    }
    try {
        auto token = read_(account);
        if (token.empty())
            throw WatchSyncError(401, "credential_missing");
        if (!seriesPage_)
            throw WatchSyncError(503, "mikan_unavailable");
        const auto page = seriesPage_(series);
        if (page.status != 200)
            throw WatchSyncError(502, "mikan_unavailable");
        auto subject = mikanLinkedSubject(page.body);
        if (!subject)
            throw WatchSyncError(409, "mikan_subject_unmapped");
        const auto item = document(request_("GET", "/v0/subjects/" + std::to_string(*subject), "", token));
        if (jsonId(item, "id") != *subject || jsonId(item, "type") != 2)
            throw WatchSyncError(409, "mikan_subject_unmapped");

        // 用令牌的当前用户名核验收藏；账号改名后也不能把「已看」误改为「在看」。
        const auto [identity, username] = verifiedAccount(request_, token);
        if (identity != account)
            throw WatchSyncError(401, "bangumi_account_changed");
        if (!collected(request_, *subject, username, token))
            check(request_("POST", "/v0/users/-/collections/" + std::to_string(*subject), R"({"type":3})", token), 204);
        std::lock_guard lock(db_.mutex());
        update(db_, "UPDATE subscription_collection SET completed=1,error='' WHERE account=? AND mikan_id=?", account,
               series);
    } catch (const WatchSyncError& e) {
        std::lock_guard lock(db_.mutex());
        update(db_,
               "UPDATE subscription_collection SET error=?,attempts=attempts+1,"
               "retry_at=unixepoch()+MIN(3600,60*(1 << MIN(attempts,6))) WHERE account=? AND mikan_id=?",
               std::string(e.what()), account, series);
        if (e.status == 401)
            update(db_, "UPDATE watch_sync_account SET connected=0,error=? WHERE account=?", std::string(e.what()),
                   account);
    }
}
void WatchSyncService::processOne() {
    std::lock_guard operation(operationMutex_);
    processSubscription();
    auto work = target(db_, 0);
    if (!work)
        return;
    auto& t = *work;
    try {
        auto token = read_(t.account);
        if (token.empty())
            throw WatchSyncError(401, "credential_missing");
        auto episode = resolve(request_, t, token);
        if (!t.cached) {
            std::lock_guard lock(db_.mutex());
            if (target(db_, t.media) != work)
                return;
            update(db_, "INSERT OR REPLACE INTO watch_bangumi_episode VALUES(?,?,?,?,?,?,?)", t.media, t.subject,
                   t.type, t.number, t.raw, t.source, episode);
            t.cached = episode;
        }
        if (!collected(request_, t.subject, t.username, token))
            throw WatchSyncError(409, "subject_not_collected");
        // 请求期间媒体可能被改绑或撤销已看。重读当前归属，旧队列行不能覆盖新的观看事件。
        if (target(db_, t.media) != work)
            return;
        auto response =
            request_("PUT", "/v0/users/-/collections/-/episodes/" + std::to_string(episode), R"({"type":2})", token);
        if (response.status == 404 || response.status == 400) {
            std::lock_guard lock(db_.mutex());
            update(db_, "DELETE FROM watch_bangumi_episode WHERE media_id=? AND episode_id=?", t.media, episode);
        }
        check(response, 204);
        std::lock_guard lock(db_.mutex());
        update(db_, "DELETE FROM watch_sync_queue WHERE id=?", t.queue);
    } catch (const WatchSyncError& e) {
        recordFailure(db_, t, e);
    }
}
WatchSyncStatus WatchSyncService::collect(Id media) {
    std::lock_guard operation(operationMutex_);
    auto work = target(db_, media);
    if (!work || work->subject <= 0 || work->blocked != work->subject)
        throw WatchSyncError(409, "collection_not_pending");
    auto& t = *work;
    {
        std::lock_guard lock(db_.mutex());
        auto checkIssue =
            query(db_, "SELECT 1 FROM watch_sync_queue WHERE id=? AND error='subject_not_collected'", t.queue);
        if (!row(checkIssue.get()))
            throw WatchSyncError(409, "collection_not_pending");
    }
    try {
        auto token = read_(t.account);
        if (token.empty())
            throw WatchSyncError(401, "credential_missing");
        // 旧用户名返回 404 也可能表示改名，先验证账号再判断是否需要新建收藏。
        const auto [identity, username] = verifiedAccount(request_, token);
        if (identity != t.account || username != t.username)
            throw WatchSyncError(401, "bangumi_account_changed");
        if (!collected(request_, t.subject, t.username, token)) {
            if (target(db_, media) != work)
                throw WatchSyncError(409, "collection_not_pending");
            check(request_("POST", "/v0/users/-/collections/" + std::to_string(t.subject), R"({"type":3})", token),
                  204);
        }
        std::lock_guard lock(db_.mutex());
        update(db_, "UPDATE watch_sync_queue SET error='',retry_at=0,attempts=0 WHERE account=? AND blocked_subject=?",
               t.account, t.subject);
    } catch (const WatchSyncError& e) {
        recordFailure(db_, t, e);
        throw;
    }
    return status();
}
void WatchSyncService::start() {
    if (worker_.joinable())
        return;
    const auto account = accountId(db_);
    if (account) {
        std::string failure;
        try {
            if (read_(account).empty())
                failure = "credential_missing";
        } catch (const WatchSyncError& e) {
            failure = e.what();
        }
        if (!failure.empty()) {
            std::lock_guard lock(db_.mutex());
            update(db_, "UPDATE watch_sync_account SET connected=0,error=?", failure);
        }
    }
    {
        std::lock_guard lock(waitMutex_);
        accepting_ = true;
    }
    worker_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::function<void()> action;
            std::unique_lock wait(waitMutex_);
            wake_.wait_for(wait, stop, std::chrono::seconds(5), [this] { return !actions_.empty(); });
            if (stop.stop_requested())
                break;
            if (!actions_.empty()) {
                action = std::move(actions_.front());
                actions_.pop_front();
            }
            wait.unlock();
            try {
                if (action)
                    action();
                else
                    processOne();
            } catch (const std::exception&) { /* 数据库临时不可用，保留队列并在下一轮重试。 */
            }
        }
    });
}
bool WatchSyncService::post(std::function<void()> task) {
    std::lock_guard lock(waitMutex_);
    if (!accepting_ || actions_.size() >= 16)
        return false;
    actions_.push_back(std::move(task));
    wake_.notify_one();
    return true;
}
void WatchSyncService::stop() {
    {
        std::lock_guard lock(waitMutex_);
        accepting_ = false;
        actions_.clear();
    }
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
}
} // namespace anime_vault
