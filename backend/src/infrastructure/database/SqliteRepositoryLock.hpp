#pragma once
#include "anime_vault/ports/RepositoryLock.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include <mutex>
namespace anime_vault {
class SqliteRepositoryLock final : public RepositoryLock {
public:
    explicit SqliteRepositoryLock(SqliteDatabase& database) : lock_(database.mutex()) {}
private:
    std::unique_lock<std::recursive_mutex> lock_;
};
}
