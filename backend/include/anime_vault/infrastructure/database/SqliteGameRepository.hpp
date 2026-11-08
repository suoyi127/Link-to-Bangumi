#pragma once
#include "anime_vault/ports/GameRepository.hpp"
namespace anime_vault {
class SqliteDatabase;
class SqliteGameRepository final : public GameRepository {
public:
    explicit SqliteGameRepository(SqliteDatabase& database) : db_(database) {}
    std::unique_ptr<RepositoryLock> lock() const override;
    std::vector<GameResource> list() const override;
    std::int64_t canonicalId(std::int64_t id) const override;
    void reconcileImportPaths();
    std::int64_t importExecutable(const std::string& path, const std::string& title, bool* reused = nullptr) override;
    std::int64_t importSteam(const GameResource& game);
    std::size_t importSteamBatch(std::vector<GameResource>& games) override;
    void updateSteamStats(const GameResource& game) override;
    void updateSteamMetadata(std::int64_t id, const std::string& appId, const std::string& chineseTitle, const std::optional<CachedCover>& cover) override;
    bool pathUsedByOther(const std::string& path, std::int64_t id) const override;
    void updateMetadata(std::int64_t id, const std::string& title, const std::string& developer, const std::string& summary, const std::string& path) override;
    void remove(std::int64_t id) override;
    void bind(std::int64_t id, std::int64_t subjectId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) override;
    bool setCover(std::int64_t id, std::int64_t subjectId, const std::string& bytes, const std::string& mime) override;
    std::optional<CachedCover> cover(std::int64_t id) const override;
    void bindVndb(std::int64_t id, const std::string& vndbId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) override;
    bool setVndbCover(std::int64_t id, const std::string& vndbId, const std::string& bytes, const std::string& mime) override;
private:
    std::int64_t upsertSteam(const GameResource& game);
    void linkDuplicates();
    bool link(const GameResource& duplicate, const GameResource& target);
    SqliteDatabase& db_;
};
}
