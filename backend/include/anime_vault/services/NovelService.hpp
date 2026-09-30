#pragma once
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/services/CoverScraper.hpp"
#include <stdexcept>
#include <vector>

namespace anime_vault {
class ProcessLauncher;
struct NovelError : std::runtime_error {
    explicit NovelError(std::string code) : std::runtime_error(code), code(std::move(code)) {}
    std::string code;
};
struct NovelFile {
    std::int64_t id{}, workId{};
    std::string path, label;
    bool missing{};
    std::optional<std::int64_t> subjectId;
    bool hasCover{};
};
struct NovelWork {
    std::int64_t id{};
    std::string title, author, summary;
    std::optional<std::int64_t> subjectId;
    bool manualMetadata{}, hasCover{};
    std::vector<NovelFile> files;
};
struct NovelSource { std::int64_t id{}; std::string path; bool directory{}; };
struct NovelImportResult { std::int64_t sourceId{}; int fileCount{}; std::vector<std::int64_t> workIds; };
struct ReaderConfig { std::string type{"system"}, executable; };

// 小说只登记原文件；作品与卷的绑定独立保存，扫描不覆盖人工编辑。
class NovelService {
public:
    explicit NovelService(SqliteDatabase& db) : db_(db) {}
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
    SqliteDatabase& db_;
};
}
