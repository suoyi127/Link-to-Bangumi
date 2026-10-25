#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <regex>

#include "SqliteSupport.hpp"
#include "SqliteRepositoryRows.hpp"

namespace anime_vault {
using namespace sqlite_support;
using namespace sqlite_rows;

std::optional<BangumiCacheRecord> SqliteMediaRepository::getBangumiCache(const std::string& key) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT query_key,response_json,expires_at,retry_count,retry_after FROM bangumi_cache WHERE query_key=?");
    sqlite_support::bind(stmt.get(), 1, key);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    if (rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    BangumiCacheRecord record{column(stmt.get(), 0), column(stmt.get(), 1),
                              std::stoll(column(stmt.get(), 2)), sqlite3_column_int(stmt.get(), 3)};
    if (sqlite3_column_type(stmt.get(), 4) != SQLITE_NULL)
        record.retryAfter = std::stoll(column(stmt.get(), 4));
    return record;
}

void SqliteMediaRepository::putBangumiCache(const BangumiCacheRecord& record) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "INSERT INTO bangumi_cache(query_key,response_json,expires_at,retry_count,retry_after) "
        "VALUES(?,?,?,?,?) ON CONFLICT(query_key) DO UPDATE SET response_json=excluded.response_json,"
        "expires_at=excluded.expires_at,retry_count=excluded.retry_count,retry_after=excluded.retry_after,"
        "updated_at=CURRENT_TIMESTAMP");
    sqlite_support::bind(stmt.get(), 1, record.queryKey);
    sqlite_support::bind(stmt.get(), 2, record.responseJson);
    sqlite_support::bind(stmt.get(), 3, std::to_string(record.expiresAt));
    sqlite3_bind_int(stmt.get(), 4, record.retryCount);
    if (record.retryAfter) sqlite_support::bind(stmt.get(), 5, std::to_string(*record.retryAfter));
    else sqlite3_bind_null(stmt.get(), 5);
    done(db, stmt.get());
}

bool SqliteMediaRepository::putBangumiFailureIfStale(const BangumiCacheRecord& record,
                                                     std::int64_t requestStartedAt) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "INSERT INTO bangumi_cache(query_key,response_json,expires_at,retry_count,retry_after) "
        "VALUES(?,?,?,?,?) ON CONFLICT(query_key) DO UPDATE SET response_json=excluded.response_json,"
        "expires_at=excluded.expires_at,retry_count=excluded.retry_count,retry_after=excluded.retry_after,"
        "updated_at=CURRENT_TIMESTAMP WHERE CAST(bangumi_cache.expires_at AS INTEGER)<=?");
    sqlite_support::bind(stmt.get(), 1, record.queryKey);
    sqlite_support::bind(stmt.get(), 2, record.responseJson);
    sqlite_support::bind(stmt.get(), 3, std::to_string(record.expiresAt));
    sqlite3_bind_int(stmt.get(), 4, record.retryCount);
    if (record.retryAfter) sqlite_support::bind(stmt.get(), 5, std::to_string(*record.retryAfter));
    else sqlite3_bind_null(stmt.get(), 5);
    sqlite3_bind_int64(stmt.get(), 6, requestStartedAt);
    done(db, stmt.get());
    return sqlite3_changes(db) == 1;
}

} // namespace anime_vault
