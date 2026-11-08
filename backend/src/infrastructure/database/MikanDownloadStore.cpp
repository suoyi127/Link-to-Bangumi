#include "anime_vault/infrastructure/database/MikanDownloadStore.hpp"
#include "SqliteSupport.hpp"
#include "anime_vault/services/MikanChinese.hpp"
#include <nlohmann/json.hpp>
namespace anime_vault {
using namespace sqlite_support;
std::int64_t MikanDownloadStore::collectionAccount() {
    std::lock_guard lock(db_.mutex());
    auto q = prepare(db_.handle(),
                     "SELECT CASE WHEN enabled THEN account ELSE 0 END FROM watch_sync_account WHERE singleton=1");
    if (sqlite3_step(q.get()) != SQLITE_ROW)
        throw std::runtime_error("watch_sync_account_unavailable");
    return sqlite3_column_int64(q.get(), 0);
}
void MikanDownloadStore::subscribe(const std::string &endpoint, const MikanSubscription &feed, std::int64_t mikanId,
                                   std::int64_t account) {
    std::lock_guard lock(db_.mutex());
    Transaction tx(db_.handle());
    if (mikanId > 0 && account > 0) {
        // 只记录真正新增的订阅；偏好修改、旧规则迁移均不补写账号收藏。
        auto intent = prepare(db_.handle(), R"sql(INSERT INTO subscription_collection(account,mikan_id,endpoint,feed)
            SELECT ?4,?1,?2,?3 WHERE NOT EXISTS
              (SELECT 1 FROM mikan_subscription WHERE endpoint=?2 AND feed=?3)
            ON CONFLICT(account,mikan_id) DO UPDATE SET endpoint=excluded.endpoint,feed=excluded.feed
              WHERE subscription_collection.ready=0 AND subscription_collection.completed=0)sql");
        sqlite3_bind_int64(intent.get(), 1, mikanId);
        sqlite_support::bind(intent.get(), 2, endpoint);
        sqlite_support::bind(intent.get(), 3, feed.url);
        sqlite3_bind_int64(intent.get(), 4, account);
        done(db_.handle(), intent.get());
    }
    auto q = prepare(db_.handle(),
                     "INSERT INTO mikan_subscription VALUES(?,?,?,?,?) ON CONFLICT(endpoint,feed) DO UPDATE SET "
                     "preferred_group=excluded.preferred_group,preferred_resolution=excluded.preferred_resolution,"
                     "preferred_subtitle=excluded.preferred_subtitle");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, feed.url);
    sqlite_support::bind(q.get(), 3, feed.group);
    sqlite_support::bind(q.get(), 4, feed.resolution);
    sqlite_support::bind(q.get(), 5, feed.subtitle);
    done(db_.handle(), q.get());
    tx.commit();
}
void MikanDownloadStore::activateCollection(const std::string &endpoint, const std::string &feedUrl) {
    std::lock_guard lock(db_.mutex());
    auto q =
        prepare(db_.handle(), "UPDATE subscription_collection SET ready=1 WHERE endpoint=? AND feed=? AND ready=0");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, feedUrl);
    done(db_.handle(), q.get());
}
std::vector<MikanSubscription> MikanDownloadStore::feeds(const std::string &endpoint) {
    std::lock_guard lock(db_.mutex());
    auto q = prepare(
        db_.handle(),
        "SELECT feed,preferred_group,preferred_resolution,preferred_subtitle FROM mikan_subscription WHERE endpoint=?");
    sqlite_support::bind(q.get(), 1, endpoint);
    std::vector<MikanSubscription> result;
    int step;
    while ((step = sqlite3_step(q.get())) == SQLITE_ROW)
        result.push_back({column(q.get(), 0), column(q.get(), 1), column(q.get(), 2), column(q.get(), 3)});
    if (step != SQLITE_DONE)
        throw std::runtime_error("mikan_read_failed");
    return result;
}
void MikanDownloadStore::learnSubtitleScripts(const std::string &endpoint, const std::string &feed,
                                              const std::map<std::string, int> &observed) {
    if (observed.empty())
        return;
    std::lock_guard lock(db_.mutex());
    nlohmann::json scripts = observed;
    auto q = prepare(db_.handle(), R"sql(
        WITH hints AS (SELECT key AS url,value AS scripts FROM json_each(?))
        UPDATE mikan_download SET subtitle_scripts=hints.scripts FROM hints
        WHERE endpoint=? AND feed=? AND subtitle_scripts=0 AND mikan_download.url=hints.url
    )sql");
    sqlite_support::bind(q.get(), 1, scripts.dump());
    sqlite_support::bind(q.get(), 2, endpoint);
    sqlite_support::bind(q.get(), 3, feed);
    done(db_.handle(), q.get());
}
void MikanDownloadStore::adoptRule(const std::string &endpoint, const std::string &feed,
                                   const std::set<std::string> &urls,
                                   const std::map<std::string, std::set<std::string>> &history,
                                   const MikanAliases &known) {
    std::lock_guard lock(db_.mutex());
    Transaction tx(db_.handle());
    auto add = prepare(db_.handle(),
                       "INSERT OR IGNORE INTO mikan_subscription(endpoint,feed,preferred_group,preferred_resolution) "
                       "VALUES(?,?,'','')");
    sqlite_support::bind(add.get(), 1, endpoint);
    sqlite_support::bind(add.get(), 2, feed);
    done(db_.handle(), add.get());
    auto seen = prepare(db_.handle(), "INSERT OR IGNORE INTO mikan_rule_baseline VALUES(?,?,?)");
    for (const auto &url : urls) {
        sqlite_support::bind(seen.get(), 1, endpoint);
        sqlite_support::bind(seen.get(), 2, feed);
        sqlite_support::bind(seen.get(), 3, url);
        done(db_.handle(), seen.get());
        sqlite3_reset(seen.get());
    }
    auto previous = prepare(db_.handle(), "INSERT OR IGNORE INTO mikan_rule_history VALUES(?,?,?,?)");
    for (const auto &[episode, identities] : history)
        for (const auto &identity : identities) {
            sqlite_support::bind(previous.get(), 1, endpoint);
            sqlite_support::bind(previous.get(), 2, feed);
            sqlite_support::bind(previous.get(), 3, episode);
            sqlite_support::bind(previous.get(), 4, identity);
            done(db_.handle(), previous.get());
            sqlite3_reset(previous.get());
        }
    auto aliases = prepare(db_.handle(), "INSERT OR IGNORE INTO mikan_title_alias VALUES(?,?,?,?)");
    for (const auto &[alias, owners] : known)
        for (const auto &owner : owners) {
            sqlite_support::bind(aliases.get(), 1, endpoint);
            sqlite_support::bind(aliases.get(), 2, feed);
            sqlite_support::bind(aliases.get(), 3, alias);
            sqlite_support::bind(aliases.get(), 4, owner);
            done(db_.handle(), aliases.get());
            sqlite3_reset(aliases.get());
        }
    tx.commit();
}
std::set<std::string> MikanDownloadStore::baseline(const std::string &endpoint, const std::string &feed) {
    std::lock_guard lock(db_.mutex());
    auto q = prepare(db_.handle(), "SELECT url FROM mikan_rule_baseline WHERE endpoint=? AND feed=?");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, feed);
    std::set<std::string> urls;
    int step;
    while ((step = sqlite3_step(q.get())) == SQLITE_ROW)
        urls.insert(column(q.get(), 0));
    if (step != SQLITE_DONE)
        throw std::runtime_error("mikan_read_failed");
    return urls;
}
bool MikanDownloadStore::wanted(const std::string &endpoint, const MikanSubscription &feed, const MikanCandidate &c) {
    std::lock_guard lock(db_.mutex());
    // 按已知的唯一别名查旧标识，保留原始历史，避免重命名后重下。
    const auto known = aliases(endpoint, "");
    auto parts = nlohmann::json::parse(resolveMikanIdentity(c.identity, known));
    const std::string title = parts.at(0).get<std::string>();
    nlohmann::json identities = nlohmann::json::array({parts.dump(), c.identity});
    for (const auto &[alias, owners] : known) {
        if (owners.size() == 1 && *owners.begin() == title) {
            parts[0] = alias;
            identities.push_back(parts.dump());
        }
    }
    auto historical = prepare(
        db_.handle(), "SELECT 1 FROM mikan_rule_history WHERE endpoint=? AND "
                      "((feed=? AND episode=? AND ?=0) OR identity IN (SELECT value FROM json_each(?))) LIMIT 1");
    sqlite_support::bind(historical.get(), 1, endpoint);
    sqlite_support::bind(historical.get(), 2, feed.url);
    sqlite_support::bind(historical.get(), 3, parts.at(3).get<std::string>());
    sqlite3_bind_int(historical.get(), 4, parts.at(2).get<int>());
    sqlite_support::bind(historical.get(), 5, identities.dump());
    const auto matched = sqlite3_step(historical.get());
    if (matched == SQLITE_ROW)
        return false;
    if (matched != SQLITE_DONE)
        throw std::runtime_error("mikan_read_failed");
    auto q =
        prepare(db_.handle(),
                "SELECT url,group_name,resolution,revision,subtitle_scripts FROM mikan_download WHERE endpoint=? AND "
                "(identity IN (SELECT value FROM json_each(?)) OR url=?)");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, identities.dump());
    sqlite_support::bind(q.get(), 3, c.url);
    int step;
    while ((step = sqlite3_step(q.get())) == SQLITE_ROW) {
        if (column(q.get(), 0) == c.url || !feed.hasPreferences())
            return false;
        auto previous = std::pair{
            mikanPreferenceScore(feed, column(q.get(), 1), column(q.get(), 2), sqlite3_column_int(q.get(), 4)),
            sqlite3_column_int(q.get(), 3)};
        if (previous >= std::pair{c.score, c.revision})
            return false;
    }
    if (step != SQLITE_DONE)
        throw std::runtime_error("mikan_read_failed");
    return true;
}
void MikanDownloadStore::record(const std::string &endpoint, const MikanSubscription &feed, const MikanCandidate &c) {
    std::lock_guard lock(db_.mutex());
    Transaction tx(db_.handle());
    forget(endpoint, c.url);
    auto q = prepare(db_.handle(),
                     "INSERT INTO mikan_download VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(endpoint,feed,url) DO NOTHING");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, feed.url);
    sqlite_support::bind(q.get(), 3, resolveMikanIdentity(c.identity, aliases(endpoint, "")));
    sqlite_support::bind(q.get(), 4, c.url);
    sqlite_support::bind(q.get(), 5, c.group);
    sqlite_support::bind(q.get(), 6, c.resolution);
    sqlite3_bind_int(q.get(), 7, c.revision);
    sqlite3_bind_int(q.get(), 8, c.subtitleScripts);
    done(db_.handle(), q.get());
    tx.commit();
}
bool MikanDownloadStore::reserve(const std::string &endpoint, const MikanSubscription &feed, const MikanCandidate &c) {
    std::lock_guard lock(db_.mutex());
    auto q = prepare(db_.handle(), "INSERT OR IGNORE INTO mikan_pending VALUES(?,?,?,?,?,?,?,?,unixepoch()+180)");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, feed.url);
    sqlite_support::bind(q.get(), 3, c.identity);
    sqlite_support::bind(q.get(), 4, c.url);
    sqlite_support::bind(q.get(), 5, c.title);
    sqlite_support::bind(q.get(), 6, c.group);
    sqlite_support::bind(q.get(), 7, c.resolution);
    sqlite3_bind_int(q.get(), 8, c.revision);
    done(db_.handle(), q.get());
    return sqlite3_changes(db_.handle()) != 0;
}
std::vector<std::pair<MikanCandidate, bool>> MikanDownloadStore::pending(const std::string &endpoint,
                                                                         const MikanSubscription &feed) {
    std::lock_guard lock(db_.mutex());
    auto q = prepare(db_.handle(), "SELECT identity,url,title,group_name,resolution,revision,retry_after<=unixepoch() "
                                   "FROM mikan_pending WHERE endpoint=? AND feed=?");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, feed.url);
    std::vector<std::pair<MikanCandidate, bool>> rows;
    int step;
    while ((step = sqlite3_step(q.get())) == SQLITE_ROW) {
        MikanCandidate c{column(q.get(), 0), column(q.get(), 1), column(q.get(), 2), column(q.get(), 3),
                         column(q.get(), 4)};
        c.revision = sqlite3_column_int(q.get(), 5);
        c.subtitleScripts = mikanSubtitleScripts(c.title);
        c.score = mikanPreferenceScore(feed, c.group, c.resolution, c.subtitleScripts);
        rows.emplace_back(std::move(c), sqlite3_column_int(q.get(), 6) != 0);
    }
    if (step != SQLITE_DONE)
        throw std::runtime_error("mikan_read_failed");
    return rows;
}
void MikanDownloadStore::forget(const std::string &endpoint, const std::string &url) {
    std::lock_guard lock(db_.mutex());
    auto q = prepare(db_.handle(), "DELETE FROM mikan_pending WHERE endpoint=? AND url=?");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, url);
    done(db_.handle(), q.get());
}
void MikanDownloadStore::retry(const std::string &endpoint, const MikanCandidate &c, int delay) {
    std::lock_guard lock(db_.mutex());
    auto q = prepare(db_.handle(), "UPDATE mikan_pending SET retry_after=unixepoch()+? WHERE endpoint=? AND url=?");
    sqlite3_bind_int(q.get(), 1, delay);
    sqlite_support::bind(q.get(), 2, endpoint);
    sqlite_support::bind(q.get(), 3, c.url);
    done(db_.handle(), q.get());
}
MikanAliases MikanDownloadStore::aliases(const std::string &endpoint, const std::string &feed) {
    std::lock_guard lock(db_.mutex());
    auto q =
        prepare(db_.handle(), "SELECT alias,canonical FROM mikan_title_alias WHERE endpoint=? AND (?='' OR feed=?)");
    sqlite_support::bind(q.get(), 1, endpoint);
    sqlite_support::bind(q.get(), 2, feed);
    sqlite_support::bind(q.get(), 3, feed);
    MikanAliases result;
    int step;
    while ((step = sqlite3_step(q.get())) == SQLITE_ROW)
        result[column(q.get(), 0)].insert(column(q.get(), 1));
    if (step != SQLITE_DONE)
        throw std::runtime_error("mikan_read_failed");
    return result;
}
void MikanDownloadStore::saveAliases(const std::string &endpoint, const std::string &feed, const MikanAliases &known) {
    std::lock_guard lock(db_.mutex());
    Transaction tx(db_.handle());
    auto q = prepare(db_.handle(), "INSERT OR IGNORE INTO mikan_title_alias VALUES(?,?,?,?)");
    for (const auto &[alias, names] : known)
        for (const auto &name : names) {
            sqlite_support::bind(q.get(), 1, endpoint);
            sqlite_support::bind(q.get(), 2, feed);
            sqlite_support::bind(q.get(), 3, alias);
            sqlite_support::bind(q.get(), 4, name);
            done(db_.handle(), q.get());
            sqlite3_reset(q.get());
        }
    tx.commit();
}
} // namespace anime_vault
