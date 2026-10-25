#include "anime_vault/services/NovelService.hpp"
#include "anime_vault/services/TitleNormalization.hpp"
#include "anime_vault/services/PlayerCatalog.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <algorithm>
#include <regex>
namespace anime_vault {
namespace fs = std::filesystem;
namespace {
std::string utf8(const fs::path& p) { auto s = p.u8string(); return {reinterpret_cast<const char*>(s.data()), s.size()}; }
fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size())); }
bool inside(const fs::path& root, const fs::path& path) {
    return std::mismatch(root.begin(), root.end(), path.begin(), path.end()).first == root.end();
}
bool supported(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".epub" || ext == ".txt" || ext == ".pdf";
}
std::string titleFor(const fs::path& path) {
    auto title = utf8(path.stem());
    // 只去掉明确的末尾卷标，不去掉普通标题中的数字。
    static const std::regex volume(R"(\s*(?:[-_ ]*(?:vol(?:ume)?\.?|第)\s*(?:[0-9]{1,3}|(?:一|二|三|四|五|六|七|八|九|十|百)+)\s*(?:卷|巻)?|\s+[0-9]{1,3}\s*(?:卷|巻)|\s*(?:\(|\[|（|【)\s*[0-9]{1,3}\s*(?:\)|\]|）|】))\s*$)", std::regex::icase);
    title = std::regex_replace(title, volume, "");
    while (!title.empty() && std::isspace(static_cast<unsigned char>(title.back()))) title.pop_back();
    return title.empty() ? utf8(path.stem()) : title;
}
std::string keyFor(const std::string& title) {
    std::string key;
    for (auto c : normalizeTitle(title)) key += std::to_string(static_cast<std::uint32_t>(c)) + ",";
    return key;
}
void requireText(const std::string& value, std::size_t limit, bool required = false) {
    if (value.size() > limit || (required && value.find_first_not_of(" \t\r\n") == std::string::npos) || value.find('\0') != std::string::npos)
        throw NovelError("invalid_novel_metadata");
}
}
std::vector<NovelSource> NovelService::sources() const {
    auto lock = repository_.lock(); return repository_.listSources();
}
std::vector<NovelWork> NovelService::list() const {
    auto lock = repository_.lock();
    auto works = repository_.listWorksWithFiles();
    std::vector<std::int64_t> absentFiles;
    for (auto& work : works) {
        // 核对全库逐卷路径；失效分卷只清记录，保留作品与来源。
        for (auto& file : work.files) {
            file.missing = !files_.isNovelFileAccessible(fromUtf8(file.path));
            if (file.missing) absentFiles.push_back(file.id);
        }
        std::erase_if(work.files, [](const NovelFile& file) { return file.missing; });
    }
    if (!absentFiles.empty()) repository_.removeFiles(absentFiles);
    return works;
}
NovelWork NovelService::get(std::int64_t id) const {
    for (auto& w : list()) if (w.id == id) return w;
    throw NovelError("novel_not_found");
}
void NovelService::removeUnavailableWork(std::int64_t id) {
    auto lock = repository_.lock();
    const auto work = get(id);
    // 删除时重新核对实际路径；只清理记录，绝不删除磁盘文件或导入来源。
    for (const auto& file : work.files) if (!file.missing) throw NovelError("novel_has_accessible_files");
    repository_.removeWorkAndFiles(id);
}
NovelImportResult NovelService::importPath(const fs::path& input) {
    std::error_code error;
    if (!input.is_absolute() || input == input.root_path() || !fs::exists(input, error) || !files_.isNovelPathSafe(input)) throw NovelError("invalid_novel_path");
    const auto path = fs::canonical(input); const bool directory = fs::is_directory(path);
    if (!directory && (!fs::is_regular_file(path) || !supported(path))) throw NovelError("novel_format_unsupported");
    auto lock = repository_.lock();
    for (const auto& s : sources()) {
        const auto existing = fromUtf8(s.path);
        if (existing != path && ((s.directory && inside(existing, path)) || (directory && inside(path, existing)))) throw NovelError("novel_source_overlap");
    }
    std::vector<fs::path> found;
    if (!directory) found.push_back(path);
    else for (fs::recursive_directory_iterator i(path), end; i != end; ++i) {
        if (files_.isLinked(i->path())) { i.disable_recursion_pending(); continue; }
        if (i->is_regular_file() && supported(i->path()) && inside(path, fs::canonical(i->path()))) found.push_back(fs::canonical(i->path()));
        if (found.size() > 20000) throw NovelError("novel_import_too_large");
    }
    // 扫描完整成功后再一次性更新缺失标记，权限或网络盘错误不会误报整批丢失。
    std::vector<NovelImportEntry> entries;
    entries.reserve(found.size());
    for (const auto& file : found) {
        const auto title = titleFor(file);
        entries.push_back({utf8(file), utf8(file.stem()), title, keyFor(title)});
    }
    return repository_.importSnapshot(utf8(path), directory, entries);
}
void NovelService::edit(std::int64_t id, std::string title, std::string author, std::string summary) {
    requireText(title, 600, true); requireText(author, 600); requireText(summary, 20000);
    auto lock = repository_.lock(); get(id);
    repository_.updateWorkMetadata(id, title, author, summary);
}
void NovelService::editFile(std::int64_t id, std::int64_t workId, std::string label) {
    requireText(label, 600, true); auto lock = repository_.lock(); get(workId);
    if (!repository_.updateFile(id, workId, label)) throw NovelError("novel_file_not_found");
}
ReaderConfig NovelService::reader() const {
    auto lock = repository_.lock(); return repository_.reader();
}
void NovelService::saveReader(const ReaderConfig& config) {
    if ((config.type != "system" && config.type != "custom") || (config.type == "custom" && !files_.isReaderExecutableValid(config.executable))) throw NovelError("invalid_reader_config");
    auto lock = repository_.lock();
    repository_.saveReader({config.type, config.type == "custom" ? normalizePlayerExecutable(config.executable) : ""});
}
void NovelService::read(std::int64_t fileId, ProcessLauncher& launcher) const {
    auto lock = repository_.lock();
    const auto target = repository_.findReadTarget(fileId);
    if (!target) throw NovelError("novel_file_not_found");
    const auto file = fromUtf8(target->path); const auto source = fromUtf8(target->sourcePath);
    // 文件恢复原位后允许立即阅读，不再被旧的扫描缺失标记拦截。
    if (!fs::is_regular_file(file)) throw NovelError("novel_file_missing");
    if (!files_.isNovelPathSafe(file) || !supported(file) || (target->sourceDirectory ? !inside(source, fs::canonical(file)) : fs::canonical(file) != source)) throw NovelError("novel_path_unsafe");
    const auto config = reader();
    if (config.type == "system") { if (!launcher.openDefault(file)) throw NovelError("reader_launch_failed"); }
    else {
        if (!files_.isReaderExecutableValid(config.executable)) throw NovelError("reader_executable_missing");
        if (!launcher.launch(fromUtf8(config.executable), {utf8(file)})) throw NovelError("reader_launch_failed");
    }
}
void NovelService::bind(std::int64_t id, bool file, std::int64_t subjectId, const std::string& title, const std::string& author, const std::string& summary, bool manual) {
    if (subjectId <= 0) throw NovelError("invalid_subject_id");
    requireText(title, 600, true); requireText(author, 600); requireText(summary, 20000);
    auto lock = repository_.lock();
    if (!repository_.bind(id, file, subjectId, title, author, summary)) throw NovelError("novel_not_found");
    (void)manual;
}
bool NovelService::setCover(std::int64_t id, bool file, std::int64_t expectedSubject, const std::string& bytes, const std::string& mime) {
    auto lock = repository_.lock(); return repository_.setCover(id, file, expectedSubject, bytes, mime);
}
std::optional<CachedCover> NovelService::cover(std::int64_t id, bool file) const {
    auto lock = repository_.lock(); return repository_.cover(id, file);
}
}
