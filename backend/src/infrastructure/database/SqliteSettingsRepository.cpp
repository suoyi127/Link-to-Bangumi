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

AuditPage SqliteMediaRepository::listAuditPage(std::int64_t offset, int limit,
                                                std::optional<std::int64_t> animeId) const {
    if (offset < 0 || offset > 1'000'000 || limit < 1 || limit > 100 ||
        (animeId && *animeId <= 0)) throw std::invalid_argument("invalid audit page");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, animeId
        ? "SELECT id,action,entity_type,entity_id,created_at FROM audit_log WHERE action IN ('anime_bound','anime_merged') AND entity_type='anime' AND entity_id=? ORDER BY id DESC LIMIT ? OFFSET ?"
        : "SELECT id,action,entity_type,entity_id,created_at FROM audit_log ORDER BY id DESC LIMIT ? OFFSET ?");
    int parameter = 1;
    if (animeId) sqlite_support::bind(stmt.get(), parameter++, std::to_string(*animeId));
    sqlite3_bind_int(stmt.get(), parameter++, limit + 1);
    sqlite3_bind_int64(stmt.get(), parameter, offset);
    AuditPage page;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
        page.items.push_back({sqlite3_column_int64(stmt.get(), 0), column(stmt.get(), 1),
                              column(stmt.get(), 2), column(stmt.get(), 3), column(stmt.get(), 4)});
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    if (page.items.size() > static_cast<std::size_t>(limit)) {
        page.items.resize(limit);
        page.nextOffset = offset + limit;
    }
    return page;
}

UiPreferences SqliteMediaRepository::getUiPreferences() const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT key,json_extract(value_json,'$') FROM setting WHERE key IN ('preferredOperation','scanIntervalSeconds','mpvExecutable','qbWebUiUrl','qbDownloadDirectory','playerType','playerExecutable')");
    UiPreferences preferences;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto key = column(stmt.get(), 0);
        if (key == "preferredOperation") preferences.preferredOperation = column(stmt.get(), 1);
        else if (key == "scanIntervalSeconds") preferences.scanIntervalSeconds = sqlite3_column_int(stmt.get(), 1);
        else if (key == "mpvExecutable") preferences.mpvExecutable = column(stmt.get(), 1);
        else if (key == "qbWebUiUrl") preferences.qbWebUiUrl = column(stmt.get(), 1);
        else if (key == "qbDownloadDirectory") preferences.qbDownloadDirectory = column(stmt.get(), 1);
        else if (key == "playerType") preferences.playerType = column(stmt.get(), 1);
        else if (key == "playerExecutable") preferences.playerExecutable = column(stmt.get(), 1);
    }
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    return preferences;
}

void SqliteMediaRepository::putUiPreferences(const UiPreferences& preferences) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto stmt = prepare(db, "INSERT INTO setting(key,value_json) VALUES(?,json_quote(?)) ON CONFLICT(key) DO UPDATE SET value_json=excluded.value_json,updated_at=CURRENT_TIMESTAMP");
    for (const auto& [key, value] : {std::pair{"preferredOperation", preferences.preferredOperation},
                                    {"scanIntervalSeconds", std::to_string(preferences.scanIntervalSeconds)},
                                    {"mpvExecutable", preferences.mpvExecutable},
                                    {"qbWebUiUrl", preferences.qbWebUiUrl},
                                    {"qbDownloadDirectory", preferences.qbDownloadDirectory},
                                    {"playerType", preferences.playerType},
                                    {"playerExecutable", preferences.playerExecutable}}) {
        sqlite3_reset(stmt.get());
        sqlite3_clear_bindings(stmt.get());
        sqlite_support::bind(stmt.get(), 1, key);
        sqlite_support::bind(stmt.get(), 2, value);
        done(db, stmt.get());
    }
    tx.commit();
}

} // namespace anime_vault
