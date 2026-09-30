#pragma once
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/services/CoverScraper.hpp"
#include <stdexcept>
#include <vector>

namespace anime_vault {
class ProcessLauncher;
struct GameError : std::runtime_error {
    explicit GameError(std::string value) : std::runtime_error(value), code(std::move(value)) {}
    std::string code;
};
struct GameResource {
    std::int64_t id{};
    std::string path, title, developer, summary, platform, vndbId, metadataSource;
    std::optional<std::int64_t> subjectId;
    bool manualMetadata{}, hasCover{}, missing{};
};
class GameService {
public:
    explicit GameService(SqliteDatabase& database) : db_(database) {}
    std::vector<GameResource> list() const;
    GameResource get(std::int64_t id) const;
    GameResource importExecutable(const std::filesystem::path& path);
    void edit(std::int64_t id, const std::string& title, const std::string& developer, const std::string& summary, const std::string& path);
    void removeUnavailable(std::int64_t id);
    void launch(std::int64_t id, ProcessLauncher& launcher) const;
    void bind(std::int64_t id, std::int64_t subjectId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform);
    bool setCover(std::int64_t id, std::int64_t subjectId, const std::string& bytes, const std::string& mime);
    void bindVndb(std::int64_t id, const std::string& vndbId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform);
    bool setVndbCover(std::int64_t id, const std::string& vndbId, const std::string& bytes, const std::string& mime);
    std::optional<CachedCover> cover(std::int64_t id) const;
private:
    SqliteDatabase& db_;
};
}
