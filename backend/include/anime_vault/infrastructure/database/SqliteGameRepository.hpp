#pragma once
#include "anime_vault/ports/GameRepository.hpp"
namespace anime_vault {
class SqliteDatabase;
class SqliteGameRepository final : public GameRepository {
public:
    explicit SqliteGameRepository(SqliteDatabase& database) : db_(database) {}
    std::unique_ptr<RepositoryLock> lock() const override;
    std::vector<GameResource> list() const override;
    std::int64_t importExecutable(const std::string& path, const std::string& title) override;
    bool pathUsedByOther(const std::string& path, std::int64_t id) const override;
    void updateMetadata(std::int64_t id, const std::string& title, const std::string& developer, const std::string& summary, const std::string& path) override;
    void remove(std::int64_t id) override;
    void bind(std::int64_t id, std::int64_t subjectId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) override;
    bool setCover(std::int64_t id, std::int64_t subjectId, const std::string& bytes, const std::string& mime) override;
    std::optional<CachedCover> cover(std::int64_t id) const override;
    void bindVndb(std::int64_t id, const std::string& vndbId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) override;
    bool setVndbCover(std::int64_t id, const std::string& vndbId, const std::string& bytes, const std::string& mime) override;
private:
    SqliteDatabase& db_;
};
}
