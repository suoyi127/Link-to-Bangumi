#pragma once

#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace anime_vault::sqlite_support {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
inline Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
    return Statement(raw, sqlite3_finalize);
}
inline void bind(sqlite3_stmt* stmt, int index, const std::string& value) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        throw std::runtime_error("sqlite bind failed");
}
inline void done(sqlite3* db, sqlite3_stmt* stmt) {
    if (sqlite3_step(stmt) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
}
inline std::string column(sqlite3_stmt* stmt, int index) {
    const auto* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}
inline void exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : "sqlite transaction failed";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
struct Transaction {
    sqlite3* db;
    bool committed{};
    explicit Transaction(sqlite3* value) : db(value) { exec(db, "BEGIN IMMEDIATE"); }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;
    ~Transaction() noexcept { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { exec(db, "COMMIT"); committed = true; }
};

} // namespace anime_vault::sqlite_support
