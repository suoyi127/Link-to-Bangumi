#include "anime_vault/infrastructure/database/WatchProgressStore.hpp"
#include "SqliteSupport.hpp"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace anime_vault {
using namespace sqlite_support;
namespace {
std::int64_t tick(sqlite3 *db) {
    exec(db, "UPDATE watch_clock SET value=value+1 WHERE singleton=1");
    auto q = prepare(db, "SELECT value FROM watch_clock WHERE singleton=1");
    if (sqlite3_step(q.get()) != SQLITE_ROW)
        throw std::runtime_error("watch_clock_missing");
    return sqlite3_column_int64(q.get(), 0);
}
std::string episodeKey(sqlite3 *db, std::int64_t mediaId) {
    auto q = prepare(db, "SELECT episode_key FROM watch_episode WHERE media_id=?");
    sqlite3_bind_int64(q.get(), 1, mediaId);
    if (sqlite3_step(q.get()) != SQLITE_ROW)
        throw std::out_of_range("media_not_found");
    return column(q.get(), 0);
}
WatchProgress readRow(sqlite3_stmt *stmt) {
    return {sqlite3_column_int64(stmt, 0), sqlite3_column_int(stmt, 1) != 0, sqlite3_column_double(stmt, 2),
            sqlite3_column_double(stmt, 3), sqlite3_column_int64(stmt, 4)};
}
WatchProgress get(sqlite3 *db, std::int64_t id) {
    auto q =
        prepare(db, "SELECT media_id,watched,position,duration,updated_at FROM watch_episode_state WHERE media_id=?");
    sqlite3_bind_int64(q.get(), 1, id);
    if (sqlite3_step(q.get()) != SQLITE_ROW)
        throw std::out_of_range("media_not_found");
    return readRow(q.get());
}
using Range = std::pair<double, double>;
std::vector<Range> mergeRanges(const std::string &saved, const std::vector<Range> &played, double duration) {
    auto ranges = nlohmann::json::parse(saved).get<std::vector<Range>>();
    ranges.insert(ranges.end(), played.begin(), played.end());
    std::sort(ranges.begin(), ranges.end());
    std::vector<Range> result;
    for (auto [lo, hi] : ranges) {
        if (!std::isfinite(lo) || !std::isfinite(hi))
            continue;
        lo = std::max(0.0, lo);
        if (duration > 0)
            hi = std::min(hi, duration);
        if (hi <= lo)
            continue;
        if (result.empty() || lo > result.back().second)
            result.emplace_back(lo, hi);
        else
            result.back().second = std::max(result.back().second, hi);
    }
    return result;
}
} // namespace
std::optional<double> WatchProgressStore::resumePosition(std::int64_t mediaId) {
    std::lock_guard lock(db_.mutex());
    const auto state = get(db_.handle(), mediaId);
    if (state.watched) return 0.0;
    if (!state.updatedAt) return std::nullopt;
    return state.position;
}
WatchSession WatchProgressStore::begin(std::int64_t mediaId, bool fromStart) {
    std::lock_guard lock(db_.mutex());
    auto *db = db_.handle();
    Transaction tx(db);
    auto key = episodeKey(db, mediaId);
    const auto state = get(db, mediaId);
    // 同集旧窗口仍可播放，但其迟到的采样不能覆盖新窗口。
    auto cancel = prepare(db, "UPDATE watch_progress SET session_id=0 WHERE media_id IN (SELECT media_id FROM "
                              "watch_episode WHERE episode_key=?)");
    sqlite_support::bind(cancel.get(), 1, key);
    done(db, cancel.get());
    const auto session = tick(db);
    auto q = prepare(db, "INSERT INTO watch_progress(media_id,session_id) VALUES(?,?) ON CONFLICT(media_id) DO UPDATE "
                         "SET session_id=excluded.session_id");
    sqlite3_bind_int64(q.get(), 1, mediaId);
    sqlite3_bind_int64(q.get(), 2, session);
    done(db, q.get());
    tx.commit();
    return {session, fromStart || state.watched ? 0 : state.position};
}
void WatchProgressStore::sample(std::int64_t mediaId, std::int64_t sessionId, double position, double duration,
                                const std::vector<Range> &ranges) {
    if (sessionId <= 0 || !std::isfinite(position) || !std::isfinite(duration) || position < 0 || duration < 0)
        return;
    std::lock_guard lock(db_.mutex());
    auto *db = db_.handle();
    Transaction tx(db);
    auto old = prepare(db, "SELECT played_ranges,duration FROM watch_progress WHERE media_id=? AND session_id=?");
    sqlite3_bind_int64(old.get(), 1, mediaId);
    sqlite3_bind_int64(old.get(), 2, sessionId);
    if (sqlite3_step(old.get()) != SQLITE_ROW)
        return;
    if (duration == 0)
        duration = sqlite3_column_double(old.get(), 1);
    auto merged = mergeRanges(column(old.get(), 0), ranges, duration);
    double covered = 0;
    for (const auto &[start, end] : merged)
        covered += end - start;
    if (duration > 0)
        position = std::min(position, duration);
    auto q = prepare(
        db, R"sql(UPDATE watch_progress SET position=?,duration=?,played_ranges=?,covered=?,updated_at=unixepoch(),
        activity_id=?,watched=MAX(watched,?) WHERE media_id=? AND session_id=?)sql");
    sqlite3_bind_double(q.get(), 1, position);
    sqlite3_bind_double(q.get(), 2, duration);
    sqlite_support::bind(q.get(), 3, nlohmann::json(merged).dump());
    sqlite3_bind_double(q.get(), 4, covered);
    sqlite3_bind_int64(q.get(), 5, tick(db));
    const bool completed = duration > 0 && covered >= 0.90 * duration;
    sqlite3_bind_int(q.get(), 6, completed && !get(db, mediaId).watched);
    sqlite3_bind_int64(q.get(), 7, mediaId);
    sqlite3_bind_int64(q.get(), 8, sessionId);
    done(db, q.get());
    tx.commit();
}
WatchProgress WatchProgressStore::mark(std::int64_t mediaId, bool watched) {
    std::lock_guard lock(db_.mutex());
    auto *db = db_.handle();
    Transaction tx(db);
    auto key = episodeKey(db, mediaId);
    if (watched) {
        if (!get(db, mediaId).watched) {
            auto q = prepare(
                db, "INSERT INTO watch_progress(media_id,watched,updated_at,activity_id) VALUES(?,1,unixepoch(),?) "
                    "ON CONFLICT(media_id) DO UPDATE SET "
                    "watched=1,updated_at=unixepoch(),activity_id=excluded.activity_id");
            sqlite3_bind_int64(q.get(), 1, mediaId);
            sqlite3_bind_int64(q.get(), 2, tick(db));
            done(db, q.get());
        }
    } else {
        auto q = prepare(
            db,
            R"sql(INSERT INTO watch_progress(media_id,updated_at)
                SELECT media_id,unixepoch() FROM watch_episode WHERE episode_key=?
                ON CONFLICT(media_id) DO UPDATE SET watched=0,position=0,covered=0,played_ranges='[]',
                updated_at=excluded.updated_at,activity_id=0,session_id=0)sql");
        sqlite_support::bind(q.get(), 1, key);
        done(db, q.get());
    }
    auto result = get(db, mediaId);
    tx.commit();
    return result;
}
std::vector<WatchProgress> WatchProgressStore::list(std::int64_t animeId) {
    std::lock_guard lock(db_.mutex());
    auto *db = db_.handle();
    auto q = prepare(db, "SELECT media_id,watched,position,duration,updated_at FROM watch_episode_state WHERE "
                         "anime_id=? ORDER BY media_id");
    sqlite3_bind_int64(q.get(), 1, animeId);
    std::vector<WatchProgress> result;
    int rc;
    while ((rc = sqlite3_step(q.get())) == SQLITE_ROW)
        result.push_back(readRow(q.get()));
    if (rc != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db));
    return result;
}
std::vector<ContinueEpisode> WatchProgressStore::continuing() {
    std::lock_guard lock(db_.mutex());
    auto *db = db_.handle();
    // 只推荐紧接的正片；不同季、特别篇以及缺失文件不会被误当作下一集。
    auto q = prepare(db, R"sql(WITH cards AS (
       SELECT s.*,0 AS next_ep,s.activity_id AS recent FROM watch_episode_state s
        WHERE s.anime_id IS NOT NULL AND s.status!='missing' AND NOT s.watched AND s.covered>=30 AND s.position>0
       UNION ALL
       SELECT n.*,1,s.activity_id FROM watch_episode_state s JOIN watch_episode_state n
        ON n.anime_id=s.anime_id AND n.season=s.season AND n.episode_type='normal'
         AND n.episode_no=CAST(CAST(s.episode_no AS INTEGER)+1 AS TEXT)
        WHERE s.watched AND s.activity_id>0 AND s.episode_type='normal'
         AND s.episode_no=CAST(CAST(s.episode_no AS INTEGER) AS TEXT)
         AND NOT n.watched AND n.status!='missing'
    ), ranked AS (
       SELECT *,ROW_NUMBER() OVER (PARTITION BY anime_id ORDER BY recent DESC,next_ep,covered DESC,media_id) AS rn FROM cards
    )
    SELECT anime_id,media_id,title,episode_number,episode_type,position,duration,next_ep FROM ranked
    WHERE rn=1 ORDER BY recent DESC,anime_id LIMIT 100)sql");
    std::vector<ContinueEpisode> result;
    int rc;
    while ((rc = sqlite3_step(q.get())) == SQLITE_ROW)
        result.push_back({sqlite3_column_int64(q.get(), 0), sqlite3_column_int64(q.get(), 1), column(q.get(), 2),
                          column(q.get(), 3), column(q.get(), 4), sqlite3_column_double(q.get(), 5),
                          sqlite3_column_double(q.get(), 6), sqlite3_column_int(q.get(), 7) != 0});
    if (rc != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db));
    return result;
}
} // namespace anime_vault
