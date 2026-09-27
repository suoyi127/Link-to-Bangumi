#pragma once

#include <filesystem>
#include <mutex>

struct sqlite3;

namespace anime_vault {

class SqliteDatabase {
public:
    explicit SqliteDatabase(const std::filesystem::path& path);
    ~SqliteDatabase();
    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;
    void migrate();
    int schemaVersion() const;
    sqlite3* handle() const noexcept { return db_; }
    std::recursive_mutex& mutex() const noexcept { return mutex_; }
private:
    sqlite3* db_{};
    mutable std::recursive_mutex mutex_;
};

} // namespace anime_vault
