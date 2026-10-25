#pragma once
#include "anime_vault/ports/GameRepository.hpp"
#include "anime_vault/ports/LibraryFileAccess.hpp"
namespace anime_vault {
class ProcessLauncher;
class GameService {
public:
    GameService(GameRepository& repository, LibraryFileAccess& files) : repository_(repository), files_(files) {}
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
    GameRepository& repository_;
    LibraryFileAccess& files_;
};
}
