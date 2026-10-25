#pragma once
#include "anime_vault/domain/Novel.hpp"
#include "anime_vault/domain/CachedCover.hpp"
#include "anime_vault/ports/RepositoryLock.hpp"
namespace anime_vault {
class NovelRepository {
public:
    virtual ~NovelRepository() = default;
    virtual std::unique_ptr<RepositoryLock> lock() const = 0;
    virtual std::vector<NovelSource> listSources() const = 0;
    virtual std::vector<NovelWork> listWorksWithFiles() const = 0;
    virtual void removeFiles(const std::vector<std::int64_t>& ids) = 0;
    virtual void removeWorkAndFiles(std::int64_t id) = 0;
    virtual NovelImportResult importSnapshot(const std::string& path, bool directory, const std::vector<NovelImportEntry>& entries) = 0;
    virtual void updateWorkMetadata(std::int64_t id, const std::string& title, const std::string& author, const std::string& summary) = 0;
    virtual bool updateFile(std::int64_t id, std::int64_t workId, const std::string& label) = 0;
    virtual ReaderConfig reader() const = 0;
    virtual void saveReader(const ReaderConfig& config) = 0;
    virtual std::optional<NovelReadTarget> findReadTarget(std::int64_t fileId) const = 0;
    virtual bool bind(std::int64_t id, bool file, std::int64_t subjectId, const std::string& title, const std::string& author, const std::string& summary) = 0;
    virtual bool setCover(std::int64_t id, bool file, std::int64_t expectedSubject, const std::string& bytes, const std::string& mime) = 0;
    virtual std::optional<CachedCover> cover(std::int64_t id, bool file) const = 0;
};
}
