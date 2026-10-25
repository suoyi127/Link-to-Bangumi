#pragma once
#include "anime_vault/ports/NovelRepository.hpp"
namespace anime_vault {
class SqliteDatabase;
class SqliteNovelRepository final : public NovelRepository {
public:
    explicit SqliteNovelRepository(SqliteDatabase& database) : db_(database) {}
    std::unique_ptr<RepositoryLock> lock() const override;
    std::vector<NovelSource> listSources() const override;
    std::vector<NovelWork> listWorksWithFiles() const override;
    void removeFiles(const std::vector<std::int64_t>& ids) override;
    void removeWorkAndFiles(std::int64_t id) override;
    NovelImportResult importSnapshot(const std::string& path, bool directory, const std::vector<NovelImportEntry>& entries) override;
    void updateWorkMetadata(std::int64_t id, const std::string& title, const std::string& author, const std::string& summary) override;
    bool updateFile(std::int64_t id, std::int64_t workId, const std::string& label) override;
    ReaderConfig reader() const override;
    void saveReader(const ReaderConfig& config) override;
    std::optional<NovelReadTarget> findReadTarget(std::int64_t fileId) const override;
    bool bind(std::int64_t id, bool file, std::int64_t subjectId, const std::string& title, const std::string& author, const std::string& summary) override;
    bool setCover(std::int64_t id, bool file, std::int64_t expectedSubject, const std::string& bytes, const std::string& mime) override;
    std::optional<CachedCover> cover(std::int64_t id, bool file) const override;
private:
    SqliteDatabase& db_;
};
}
