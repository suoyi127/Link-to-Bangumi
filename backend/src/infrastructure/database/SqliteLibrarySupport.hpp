#pragma once
#include "SqliteSupport.hpp"
#include <utility>
namespace anime_vault::sqlite_library_support {
template<class Error, class Action>
decltype(auto) storageCall(Action&& action, const char* code) {
    try { return std::forward<Action>(action)(); }
    catch (const Error&) { throw; }
    catch (const std::runtime_error&) { throw Error(code); }
}
template<class Error>
sqlite_support::Statement prepare(sqlite3* db, const char* sql, const char* code) {
    return storageCall<Error>([&] { return sqlite_support::prepare(db, sql); }, code);
}
template<class Error>
void done(sqlite3_stmt* statement, const char* code) {
    storageCall<Error>([&] { sqlite_support::done(sqlite3_db_handle(statement), statement); }, code);
}
}
