#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace anime_vault {
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

struct NovelImportEntry { std::string path, label, title, importKey; };
struct NovelReadTarget { std::string path, sourcePath; bool sourceDirectory{}; };
}
