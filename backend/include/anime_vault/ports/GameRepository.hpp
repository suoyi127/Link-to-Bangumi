#pragma once
#include "anime_vault/domain/Game.hpp"
#include "anime_vault/domain/CachedCover.hpp"
#include "anime_vault/ports/RepositoryLock.hpp"
namespace anime_vault {
class GameRepository {
public:
    virtual ~GameRepository() = default;
    virtual std::unique_ptr<RepositoryLock> lock() const = 0;
    virtual std::vector<GameResource> list() const = 0;
    virtual std::int64_t importExecutable(const std::string& path, const std::string& title) = 0;
    virtual bool pathUsedByOther(const std::string& path, std::int64_t id) const = 0;
    virtual void updateMetadata(std::int64_t id, const std::string& title, const std::string& developer, const std::string& summary, const std::string& path) = 0;
    virtual void remove(std::int64_t id) = 0;
    virtual void bind(std::int64_t id, std::int64_t subjectId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) = 0;
    virtual bool setCover(std::int64_t id, std::int64_t subjectId, const std::string& bytes, const std::string& mime) = 0;
    virtual std::optional<CachedCover> cover(std::int64_t id) const = 0;
    virtual void bindVndb(std::int64_t id, const std::string& vndbId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) = 0;
    virtual bool setVndbCover(std::int64_t id, const std::string& vndbId, const std::string& bytes, const std::string& mime) = 0;
};
}
