#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "InitialSql.hpp"

#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace anime_vault {
namespace {
void exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
}

SqliteDatabase::SqliteDatabase(const std::filesystem::path& path) {
    const auto name = path.u8string();
    if (sqlite3_open(reinterpret_cast<const char*>(name.c_str()), &db_) != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(message);
    }
    try { exec(db_, "PRAGMA foreign_keys=ON"); }
    catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}

SqliteDatabase::~SqliteDatabase() { sqlite3_close(db_); }

int SqliteDatabase::schemaVersion() const {
    std::lock_guard lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA user_version", -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db_));
    const int result = sqlite3_step(stmt);
    const int version = result == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    if (result != SQLITE_ROW) throw std::runtime_error("cannot read schema version");
    return version;
}

void SqliteDatabase::migrate() {
    std::lock_guard lock(mutex_);
    const int version = schemaVersion();
    if (version > 7 || version < 0) throw std::runtime_error("unsupported schema version");
    const char* migrations[] = {kInitialSql, kCorrectionSql, kSourceSnapshotSql,
                                kMediaOriginSql, kOrganizationExecutionSql, kParsedTitleSql,
                                kFolderImportSql};
    for (int next = version + 1; next <= 7; ++next) {
        exec(db_, "BEGIN IMMEDIATE");
        try {
            exec(db_, migrations[next - 1]);
            exec(db_, ("PRAGMA user_version=" + std::to_string(next)).c_str());
            exec(db_, "COMMIT");
        } catch (...) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
    }
}
} // namespace anime_vault
