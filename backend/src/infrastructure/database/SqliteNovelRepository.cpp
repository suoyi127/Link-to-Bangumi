#include "anime_vault/infrastructure/database/SqliteNovelRepository.hpp"
#include "SqliteRepositoryLock.hpp"
#include "SqliteLibrarySupport.hpp"
#include <nlohmann/json.hpp>
#include <set>
namespace anime_vault {
namespace {
using Statement = sqlite_support::Statement;
Statement prepare(sqlite3* db, const std::string& sql) {
    return sqlite_library_support::prepare<NovelError>(db, sql.c_str(), "novel_storage_error");
}
void bindText(Statement& stmt, int column, const std::string& value) {
    sqlite3_bind_text(stmt.get(), column, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}
void done(Statement& stmt) { sqlite_library_support::done<NovelError>(stmt.get(), "novel_storage_error"); }
std::string text(sqlite3_stmt* stmt, int column) { return sqlite_support::column(stmt, column); }
}
std::unique_ptr<RepositoryLock> SqliteNovelRepository::lock() const { return std::make_unique<SqliteRepositoryLock>(db_); }
std::vector<NovelSource> SqliteNovelRepository::listSources() const {
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), "SELECT id,path,is_directory FROM novel_source ORDER BY id");
    std::vector<NovelSource> out; int state;
    while ((state = sqlite3_step(stmt.get())) == SQLITE_ROW) out.push_back({sqlite3_column_int64(stmt.get(), 0), text(stmt.get(), 1), sqlite3_column_int(stmt.get(), 2) != 0});
    if (state != SQLITE_DONE) throw NovelError("novel_storage_error");
    return out;
}
std::vector<NovelWork> SqliteNovelRepository::listWorksWithFiles() const {
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
            f.missing = sqlite3_column_int(files.get(), 3) != 0;
            if (sqlite3_column_type(files.get(), 4) != SQLITE_NULL) f.subjectId = sqlite3_column_int64(files.get(), 4);
            f.hasCover = sqlite3_column_int(files.get(), 5) != 0; w.files.push_back(std::move(f));
        }
        if (fileState != SQLITE_DONE) throw NovelError("novel_storage_error");
        out.push_back(std::move(w));
    }
    if (state != SQLITE_DONE) throw NovelError("novel_storage_error");
    return out;
}
void SqliteNovelRepository::removeFiles(const std::vector<std::int64_t>& absentFiles) {
    std::lock_guard lock(db_.mutex());
    if (!absentFiles.empty()) {
        auto begin = prepare(db_.handle(), "BEGIN IMMEDIATE"); done(begin);
        try {
            auto remove = prepare(db_.handle(), "DELETE FROM novel_file WHERE id=?");
            for (const auto id : absentFiles) {
                sqlite3_reset(remove.get()); sqlite3_bind_int64(remove.get(), 1, id); done(remove);
            }
            auto commit = prepare(db_.handle(), "COMMIT"); done(commit);
        } catch (...) { sqlite3_exec(db_.handle(), "ROLLBACK", nullptr, nullptr, nullptr); throw; }
    }
}
void SqliteNovelRepository::removeWorkAndFiles(std::int64_t id) {
    std::lock_guard lock(db_.mutex());
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
NovelImportResult SqliteNovelRepository::importSnapshot(const std::string& path, bool directory, const std::vector<NovelImportEntry>& entries) {
    std::lock_guard lock(db_.mutex());
    auto db = db_.handle(); NovelImportResult result; std::set<std::int64_t> works;
    auto begin = prepare(db, "BEGIN IMMEDIATE"); done(begin);
    try {
        auto add = prepare(db, "INSERT INTO novel_source(path,is_directory) VALUES(?,?) ON CONFLICT(path) DO NOTHING");
        bindText(add, 1, path); sqlite3_bind_int(add.get(), 2, directory); done(add);
        auto source = prepare(db, "SELECT id FROM novel_source WHERE path=?"); bindText(source, 1, path);
        if (sqlite3_step(source.get()) != SQLITE_ROW) throw NovelError("novel_storage_error");
        result.sourceId = sqlite3_column_int64(source.get(), 0);
        auto absent = prepare(db, "UPDATE novel_file SET missing=1 WHERE source_id=?"); sqlite3_bind_int64(absent.get(), 1, result.sourceId); done(absent);
        for (const auto& file : entries) {
            auto known = prepare(db, "SELECT work_id FROM novel_file WHERE path=?"); bindText(known, 1, file.path);
            const auto knownState = sqlite3_step(known.get()); std::int64_t workId{};
            if (knownState == SQLITE_ROW) workId = sqlite3_column_int64(known.get(), 0);
            else if (knownState == SQLITE_DONE) {
                const auto& title = file.title; const auto& key = file.importKey;
                auto work = prepare(db, "INSERT INTO novel_work(import_key,title) VALUES(?,?) ON CONFLICT(import_key) DO NOTHING"); bindText(work, 1, key); bindText(work, 2, title); done(work);
                auto id = prepare(db, "SELECT id FROM novel_work WHERE import_key=?"); bindText(id, 1, key);
                if (sqlite3_step(id.get()) != SQLITE_ROW) throw NovelError("novel_storage_error");
                workId = sqlite3_column_int64(id.get(), 0);
            } else throw NovelError("novel_storage_error");
            auto record = prepare(db, "INSERT INTO novel_file(work_id,source_id,path,label) VALUES(?,?,?,?) ON CONFLICT(path) DO UPDATE SET missing=0");
            sqlite3_bind_int64(record.get(), 1, workId); sqlite3_bind_int64(record.get(), 2, result.sourceId); bindText(record, 3, file.path); bindText(record, 4, file.label); done(record);
            works.insert(workId);
        }
        auto commit = prepare(db, "COMMIT"); done(commit);
    } catch (...) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
    result.fileCount = static_cast<int>(entries.size()); result.workIds.assign(works.begin(), works.end()); return result;
}
void SqliteNovelRepository::updateWorkMetadata(std::int64_t id, const std::string& title, const std::string& author, const std::string& summary) {
    std::lock_guard lock(db_.mutex());
    auto stmt = prepare(db_.handle(), "UPDATE novel_work SET title=?,author=?,summary=?,manual_metadata=1 WHERE id=?");
    bindText(stmt, 1, title); bindText(stmt, 2, author); bindText(stmt, 3, summary); sqlite3_bind_int64(stmt.get(), 4, id); done(stmt);
}
bool SqliteNovelRepository::updateFile(std::int64_t id, std::int64_t workId, const std::string& label) {
    std::lock_guard lock(db_.mutex());
    auto stmt = prepare(db_.handle(), "UPDATE novel_file SET work_id=?,label=? WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, workId); bindText(stmt, 2, label); sqlite3_bind_int64(stmt.get(), 3, id); done(stmt);
    return sqlite3_changes(db_.handle()) != 0;
}
ReaderConfig SqliteNovelRepository::reader() const {
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), "SELECT value_json FROM setting WHERE key='novelReader'");
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) return {};
    const auto j = nlohmann::json::parse(text(stmt.get(), 0)); return {j.at("type").get<std::string>(), j.at("executable").get<std::string>()};
}
void SqliteNovelRepository::saveReader(const ReaderConfig& config) {
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), "INSERT INTO setting(key,value_json) VALUES('novelReader',?) ON CONFLICT(key) DO UPDATE SET value_json=excluded.value_json");
    bindText(stmt, 1, nlohmann::json{{"type", config.type}, {"executable", config.executable}}.dump()); done(stmt);
}
std::optional<NovelReadTarget> SqliteNovelRepository::findReadTarget(std::int64_t fileId) const {
    std::lock_guard lock(db_.mutex());
    auto stmt = prepare(db_.handle(), "SELECT f.path,f.missing,s.path,s.is_directory FROM novel_file f JOIN novel_source s ON f.source_id=s.id WHERE f.id=?"); sqlite3_bind_int64(stmt.get(), 1, fileId);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) return {};
    return NovelReadTarget{text(stmt.get(), 0), text(stmt.get(), 2), sqlite3_column_int(stmt.get(), 3) != 0};
}
bool SqliteNovelRepository::bind(std::int64_t id, bool file, std::int64_t subjectId, const std::string& title, const std::string& author, const std::string& summary) {
    std::lock_guard lock(db_.mutex());
    const std::string sql = file ? "UPDATE novel_file SET cover=CASE WHEN subject_id=? THEN cover ELSE NULL END,subject_id=? WHERE id=?" :
        "UPDATE novel_work SET cover=CASE WHEN subject_id=? THEN cover ELSE NULL END,subject_id=?,title=CASE WHEN manual_metadata=0 THEN ? ELSE title END,author=CASE WHEN manual_metadata=0 THEN ? ELSE author END,summary=CASE WHEN manual_metadata=0 THEN ? ELSE summary END WHERE id=?";
    auto stmt = prepare(db_.handle(), sql); sqlite3_bind_int64(stmt.get(), 1, subjectId); sqlite3_bind_int64(stmt.get(), 2, subjectId);
    if (!file) { bindText(stmt, 3, title); bindText(stmt, 4, author); bindText(stmt, 5, summary); }
    sqlite3_bind_int64(stmt.get(), file ? 3 : 6, id); done(stmt);
    return sqlite3_changes(db_.handle()) != 0;
}
bool SqliteNovelRepository::setCover(std::int64_t id, bool file, std::int64_t expectedSubject, const std::string& bytes, const std::string& mime) {
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), std::string("UPDATE ") + (file ? "novel_file" : "novel_work") + " SET cover=?,cover_mime=? WHERE id=? AND subject_id=?");
    sqlite3_bind_blob(stmt.get(), 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT); bindText(stmt, 2, mime); sqlite3_bind_int64(stmt.get(), 3, id); sqlite3_bind_int64(stmt.get(), 4, expectedSubject); done(stmt); return sqlite3_changes(db_.handle()) != 0;
}
std::optional<CachedCover> SqliteNovelRepository::cover(std::int64_t id, bool file) const {
    std::lock_guard lock(db_.mutex()); auto stmt = prepare(db_.handle(), std::string("SELECT cover,cover_mime FROM ") + (file ? "novel_file" : "novel_work") + " WHERE id=? AND cover IS NOT NULL"); sqlite3_bind_int64(stmt.get(), 1, id);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) return {};
    const auto* p = static_cast<const char*>(sqlite3_column_blob(stmt.get(), 0)); const int size = sqlite3_column_bytes(stmt.get(), 0);
    return CachedCover{std::string(p, static_cast<std::size_t>(size)), text(stmt.get(), 1), ""};
}
}
