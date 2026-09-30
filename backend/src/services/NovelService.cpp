#include "anime_vault/services/NovelService.hpp"
#include "anime_vault/services/TitleNormalization.hpp"
#include "anime_vault/services/PlayerCatalog.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <regex>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace anime_vault {
namespace fs = std::filesystem;
namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement prepare(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* raw{};
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) throw NovelError("novel_storage_error");
    return {raw, sqlite3_finalize};
}
void bindText(Statement& stmt, int column, const std::string& value) {
    sqlite3_bind_text(stmt.get(), column, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}
void done(Statement& stmt) { if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw NovelError("novel_storage_error"); }
std::string text(sqlite3_stmt* s, int col) { auto p = sqlite3_column_text(s, col); return p ? reinterpret_cast<const char*>(p) : ""; }
std::string utf8(const fs::path& p) { auto s = p.u8string(); return {reinterpret_cast<const char*>(s.data()), s.size()}; }
fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size())); }
bool inside(const fs::path& root, const fs::path& path) {
    return std::mismatch(root.begin(), root.end(), path.begin(), path.end()).first == root.end();
}
bool linked(const fs::path& path) {
    if (fs::is_symlink(path)) return true;
#ifdef _WIN32
    const auto attr = GetFileAttributesW(path.c_str());
    return attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}
bool safePath(const fs::path& path) {
    // 每一级都检查，防止登记后父目录被替换为 junction 后越界打开。
    for (auto p = path; !p.empty(); p = p.parent_path()) {
        if (linked(p)) return false;
        if (p == p.root_path()) break;
    }
    return true;
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
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), "SELECT id,path,is_directory FROM novel_source ORDER BY id");
    std::vector<NovelSource> out; int state;
    while ((state = sqlite3_step(stmt.get())) == SQLITE_ROW) out.push_back({sqlite3_column_int64(stmt.get(), 0), text(stmt.get(), 1), sqlite3_column_int(stmt.get(), 2) != 0});
    if (state != SQLITE_DONE) throw NovelError("novel_storage_error");
    return out;
}
std::vector<NovelWork> NovelService::list() const {
    std::lock_guard lock(db_.mutex());
    auto stmt = prepare(db_.handle(), "SELECT id,title,author,summary,subject_id,manual_metadata,cover IS NOT NULL FROM novel_work ORDER BY title,id");
    std::vector<NovelWork> out; int state;
    while ((state = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        NovelWork w; w.id = sqlite3_column_int64(stmt.get(), 0); w.title = text(stmt.get(), 1); w.author = text(stmt.get(), 2); w.summary = text(stmt.get(), 3);
        if (sqlite3_column_type(stmt.get(), 4) != SQLITE_NULL) w.subjectId = sqlite3_column_int64(stmt.get(), 4);
        w.manualMetadata = sqlite3_column_int(stmt.get(), 5) != 0; w.hasCover = sqlite3_column_int(stmt.get(), 6) != 0;
        auto files = prepare(db_.handle(), "SELECT id,path,label,missing,subject_id,cover IS NOT NULL FROM novel_file WHERE work_id=? ORDER BY label,id");
        sqlite3_bind_int64(files.get(), 1, w.id); int fileState;
        while ((fileState = sqlite3_step(files.get())) == SQLITE_ROW) {
            NovelFile f; f.id = sqlite3_column_int64(files.get(), 0); f.workId = w.id; f.path = text(files.get(), 1); f.label = text(files.get(), 2);
            // 重扫标记不能代表当前状态；仅核对已登记路径，不删除记录或重新刮削。
            std::error_code pathError;
            const auto path = fromUtf8(f.path);
            const auto status = fs::symlink_status(path, pathError);
            f.missing = pathError || !fs::is_regular_file(status) || !safePath(path);
            if (sqlite3_column_type(files.get(), 4) != SQLITE_NULL) f.subjectId = sqlite3_column_int64(files.get(), 4);
            f.hasCover = sqlite3_column_int(files.get(), 5) != 0; w.files.push_back(std::move(f));
        }
        if (fileState != SQLITE_DONE) throw NovelError("novel_storage_error");
        out.push_back(std::move(w));
    }
    if (state != SQLITE_DONE) throw NovelError("novel_storage_error");
    return out;
}
NovelWork NovelService::get(std::int64_t id) const {
    for (auto& w : list()) if (w.id == id) return w;
    throw NovelError("novel_not_found");
}
void NovelService::removeUnavailableWork(std::int64_t id) {
    std::lock_guard lock(db_.mutex());
    const auto work = get(id);
    // 删除时重新核对实际路径；只清理记录，绝不删除磁盘文件或导入来源。
    for (const auto& file : work.files) if (!file.missing) throw NovelError("novel_has_accessible_files");
    auto* db = db_.handle();
    auto begin = prepare(db, "BEGIN IMMEDIATE"); done(begin);
    try {
        auto files = prepare(db, "DELETE FROM novel_file WHERE work_id=?");
        sqlite3_bind_int64(files.get(), 1, id); done(files);
        auto record = prepare(db, "DELETE FROM novel_work WHERE id=?");
        sqlite3_bind_int64(record.get(), 1, id); done(record);
        auto commit = prepare(db, "COMMIT"); done(commit);
    } catch (...) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
NovelImportResult NovelService::importPath(const fs::path& input) {
    std::error_code error;
    if (!input.is_absolute() || input == input.root_path() || !fs::exists(input, error) || !safePath(input)) throw NovelError("invalid_novel_path");
    const auto path = fs::canonical(input); const bool directory = fs::is_directory(path);
    if (!directory && (!fs::is_regular_file(path) || !supported(path))) throw NovelError("novel_format_unsupported");
    std::lock_guard lock(db_.mutex());
    for (const auto& s : sources()) {
        const auto existing = fromUtf8(s.path);
        if (existing != path && ((s.directory && inside(existing, path)) || (directory && inside(path, existing)))) throw NovelError("novel_source_overlap");
    }
    std::vector<fs::path> found;
    if (!directory) found.push_back(path);
    else for (fs::recursive_directory_iterator i(path), end; i != end; ++i) {
        if (linked(i->path())) { i.disable_recursion_pending(); continue; }
        if (i->is_regular_file() && supported(i->path()) && inside(path, fs::canonical(i->path()))) found.push_back(fs::canonical(i->path()));
        if (found.size() > 20000) throw NovelError("novel_import_too_large");
    }
    // 扫描完整成功后再一次性更新缺失标记，权限或网络盘错误不会误报整批丢失。
    auto db = db_.handle(); NovelImportResult result; std::set<std::int64_t> works;
    auto begin = prepare(db, "BEGIN IMMEDIATE"); done(begin);
    try {
        auto add = prepare(db, "INSERT INTO novel_source(path,is_directory) VALUES(?,?) ON CONFLICT(path) DO NOTHING");
        bindText(add, 1, utf8(path)); sqlite3_bind_int(add.get(), 2, directory); done(add);
        auto source = prepare(db, "SELECT id FROM novel_source WHERE path=?"); bindText(source, 1, utf8(path));
        if (sqlite3_step(source.get()) != SQLITE_ROW) throw NovelError("novel_storage_error");
        result.sourceId = sqlite3_column_int64(source.get(), 0);
        auto absent = prepare(db, "UPDATE novel_file SET missing=1 WHERE source_id=?"); sqlite3_bind_int64(absent.get(), 1, result.sourceId); done(absent);
        for (const auto& file : found) {
            auto known = prepare(db, "SELECT work_id FROM novel_file WHERE path=?"); bindText(known, 1, utf8(file));
            const auto knownState = sqlite3_step(known.get()); std::int64_t workId{};
            if (knownState == SQLITE_ROW) workId = sqlite3_column_int64(known.get(), 0);
            else if (knownState == SQLITE_DONE) {
                const auto title = titleFor(file); const auto key = keyFor(title);
                auto work = prepare(db, "INSERT INTO novel_work(import_key,title) VALUES(?,?) ON CONFLICT(import_key) DO NOTHING"); bindText(work, 1, key); bindText(work, 2, title); done(work);
                auto id = prepare(db, "SELECT id FROM novel_work WHERE import_key=?"); bindText(id, 1, key);
                if (sqlite3_step(id.get()) != SQLITE_ROW) throw NovelError("novel_storage_error");
                workId = sqlite3_column_int64(id.get(), 0);
            } else throw NovelError("novel_storage_error");
            auto record = prepare(db, "INSERT INTO novel_file(work_id,source_id,path,label) VALUES(?,?,?,?) ON CONFLICT(path) DO UPDATE SET missing=0");
            sqlite3_bind_int64(record.get(), 1, workId); sqlite3_bind_int64(record.get(), 2, result.sourceId); bindText(record, 3, utf8(file)); bindText(record, 4, utf8(file.stem())); done(record);
            works.insert(workId);
        }
        auto commit = prepare(db, "COMMIT"); done(commit);
    } catch (...) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
    result.fileCount = static_cast<int>(found.size()); result.workIds.assign(works.begin(), works.end()); return result;
}
void NovelService::edit(std::int64_t id, std::string title, std::string author, std::string summary) {
    requireText(title, 600, true); requireText(author, 600); requireText(summary, 20000);
    std::lock_guard lock(db_.mutex()); get(id);
    auto stmt = prepare(db_.handle(), "UPDATE novel_work SET title=?,author=?,summary=?,manual_metadata=1 WHERE id=?");
    bindText(stmt, 1, title); bindText(stmt, 2, author); bindText(stmt, 3, summary); sqlite3_bind_int64(stmt.get(), 4, id); done(stmt);
}
void NovelService::editFile(std::int64_t id, std::int64_t workId, std::string label) {
    requireText(label, 600, true); std::lock_guard lock(db_.mutex()); get(workId);
    auto stmt = prepare(db_.handle(), "UPDATE novel_file SET work_id=?,label=? WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, workId); bindText(stmt, 2, label); sqlite3_bind_int64(stmt.get(), 3, id); done(stmt);
    if (!sqlite3_changes(db_.handle())) throw NovelError("novel_file_not_found");
}
ReaderConfig NovelService::reader() const {
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), "SELECT value_json FROM setting WHERE key='novelReader'");
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) return {};
    const auto j = nlohmann::json::parse(text(stmt.get(), 0)); return {j.at("type").get<std::string>(), j.at("executable").get<std::string>()};
}
void NovelService::saveReader(const ReaderConfig& config) {
    if ((config.type != "system" && config.type != "custom") || (config.type == "custom" && !validPlayerExecutable(config.executable))) throw NovelError("invalid_reader_config");
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), "INSERT INTO setting(key,value_json) VALUES('novelReader',?) ON CONFLICT(key) DO UPDATE SET value_json=excluded.value_json");
    bindText(stmt, 1, nlohmann::json{{"type", config.type}, {"executable", config.type == "custom" ? normalizePlayerExecutable(config.executable) : ""}}.dump()); done(stmt);
}
void NovelService::read(std::int64_t fileId, ProcessLauncher& launcher) const {
    std::lock_guard lock(db_.mutex());
    auto stmt = prepare(db_.handle(), "SELECT f.path,f.missing,s.path,s.is_directory FROM novel_file f JOIN novel_source s ON f.source_id=s.id WHERE f.id=?"); sqlite3_bind_int64(stmt.get(), 1, fileId);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) throw NovelError("novel_file_not_found");
    const auto file = fromUtf8(text(stmt.get(), 0)); const auto source = fromUtf8(text(stmt.get(), 2));
    // 文件恢复原位后允许立即阅读，不再被旧的扫描缺失标记拦截。
    if (!fs::is_regular_file(file)) throw NovelError("novel_file_missing");
    if (!safePath(file) || !supported(file) || (sqlite3_column_int(stmt.get(), 3) ? !inside(source, fs::canonical(file)) : fs::canonical(file) != source)) throw NovelError("novel_path_unsafe");
    const auto config = reader();
    if (config.type == "system") { if (!launcher.openDefault(file)) throw NovelError("reader_launch_failed"); }
    else {
        if (!validPlayerExecutable(config.executable)) throw NovelError("reader_executable_missing");
        if (!launcher.launch(fromUtf8(config.executable), {utf8(file)})) throw NovelError("reader_launch_failed");
    }
}
void NovelService::bind(std::int64_t id, bool file, std::int64_t subjectId, const std::string& title, const std::string& author, const std::string& summary, bool manual) {
    if (subjectId <= 0) throw NovelError("invalid_subject_id");
    requireText(title, 600, true); requireText(author, 600); requireText(summary, 20000);
    std::lock_guard lock(db_.mutex());
    const std::string sql = file ? "UPDATE novel_file SET cover=CASE WHEN subject_id=? THEN cover ELSE NULL END,subject_id=? WHERE id=?" :
        "UPDATE novel_work SET cover=CASE WHEN subject_id=? THEN cover ELSE NULL END,subject_id=?,title=CASE WHEN manual_metadata=0 THEN ? ELSE title END,author=CASE WHEN manual_metadata=0 THEN ? ELSE author END,summary=CASE WHEN manual_metadata=0 THEN ? ELSE summary END WHERE id=?";
    auto stmt = prepare(db_.handle(), sql); sqlite3_bind_int64(stmt.get(), 1, subjectId); sqlite3_bind_int64(stmt.get(), 2, subjectId);
    if (!file) { bindText(stmt, 3, title); bindText(stmt, 4, author); bindText(stmt, 5, summary); }
    sqlite3_bind_int64(stmt.get(), file ? 3 : 6, id); done(stmt);
    if (!sqlite3_changes(db_.handle())) throw NovelError("novel_not_found");
    (void)manual;
}
bool NovelService::setCover(std::int64_t id, bool file, std::int64_t expectedSubject, const std::string& bytes, const std::string& mime) {
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), std::string("UPDATE ") + (file ? "novel_file" : "novel_work") + " SET cover=?,cover_mime=? WHERE id=? AND subject_id=?");
    sqlite3_bind_blob(stmt.get(), 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT); bindText(stmt, 2, mime); sqlite3_bind_int64(stmt.get(), 3, id); sqlite3_bind_int64(stmt.get(), 4, expectedSubject); done(stmt); return sqlite3_changes(db_.handle()) != 0;
}
std::optional<CachedCover> NovelService::cover(std::int64_t id, bool file) const {
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), std::string("SELECT cover,cover_mime FROM ") + (file ? "novel_file" : "novel_work") + " WHERE id=? AND cover IS NOT NULL"); sqlite3_bind_int64(stmt.get(), 1, id);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) return {};
    const auto* p = static_cast<const char*>(sqlite3_column_blob(stmt.get(), 0)); const int size = sqlite3_column_bytes(stmt.get(), 0);
    return CachedCover{std::string(p, static_cast<std::size_t>(size)), text(stmt.get(), 1), ""};
}
}
