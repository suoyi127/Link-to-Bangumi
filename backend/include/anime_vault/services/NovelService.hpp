#pragma once
#include "anime_vault/ports/NovelRepository.hpp"
#include "anime_vault/ports/LibraryFileAccess.hpp"
namespace anime_vault {
class ProcessLauncher;
class NovelService {
public:
    NovelService(NovelRepository& repository, LibraryFileAccess& files) : repository_(repository), files_(files) {}
    std::vector<NovelWork> list() const;
    NovelWork get(std::int64_t id) const;
    void removeUnavailableWork(std::int64_t id);
    std::vector<NovelSource> sources() const;
    NovelImportResult importPath(const std::filesystem::path& path);
    void edit(std::int64_t id, std::string title, std::string author, std::string summary);
    void editFile(std::int64_t id, std::int64_t workId, std::string label);
    ReaderConfig reader() const;
    void saveReader(const ReaderConfig& config);
    void read(std::int64_t fileId, ProcessLauncher& launcher) const;
    void bind(std::int64_t id, bool file, std::int64_t subjectId,
              const std::string& title, const std::string& author, const std::string& summary, bool manual);
    bool setCover(std::int64_t id, bool file, std::int64_t expectedSubject,
                  const std::string& bytes, const std::string& mime);
    std::optional<CachedCover> cover(std::int64_t id, bool file) const;
private:
    NovelRepository& repository_;
    LibraryFileAccess& files_;
};
}
