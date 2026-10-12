#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <sqlite3.h>
#include <memory>
#include <stdexcept>

namespace anime_vault {
namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw{};
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    return {raw, sqlite3_finalize};
}
void bind(sqlite3_stmt* stmt, int index, const std::string& value) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) throw std::runtime_error("merge bind failed");
}
std::string text(sqlite3_stmt* stmt, int index) {
    const auto* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}
void done(sqlite3* db, sqlite3_stmt* stmt) {
    if (sqlite3_step(stmt) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
}
void execute(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
}
struct Transaction {
    sqlite3* db;
    bool committed{};
    explicit Transaction(sqlite3* value) : db(value) { execute(db, "BEGIN IMMEDIATE"); }
    ~Transaction() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { execute(db, "COMMIT"); committed = true; }
};
std::int64_t scalar(sqlite3* db, const char* sql, std::int64_t source, std::int64_t target) {
    auto stmt = prepare(db, sql);
    sqlite3_bind_int64(stmt.get(), 1, source); sqlite3_bind_int64(stmt.get(), 2, target);
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    return sqlite3_column_int64(stmt.get(), 0);
}
void checkRunning(sqlite3* db, std::int64_t source, std::int64_t target) {
    if (scalar(db, "SELECT COUNT(*) FROM organize_job j JOIN organize_plan p ON p.id=j.plan_id JOIN media_file m ON m.id=p.media_file_id WHERE m.anime_id IN (?,?) AND j.status='running'", source, target))
        throw AnimeBindingError("merge_organization_running");
}
std::string snapshot(sqlite3* db, std::int64_t source, std::int64_t target) {
    std::string result;
    // 忽略扫描时间戳，但完整比较归属、资料、媒体状态、集数、路径和别名。
    for (const auto* sql : {
        "SELECT json_group_array(json(row)) FROM (SELECT json_array(id,display_title,original_title,season,year,bangumi_subject_id,cover_url,locked,created_at) row FROM anime WHERE id IN (?,?) ORDER BY id)",
        "SELECT json_group_array(json(row)) FROM (SELECT json_array(id,anime_id,title,season,episode_number,raw_episode_number,episode_number_source,episode_type,source_path,library_path,status,size_bytes,source_modified_at,origin,folder_import_id) row FROM media_file WHERE anime_id IN (?,?) ORDER BY id)",
        "SELECT json_group_array(json(row)) FROM (SELECT json_array(anime_id,normalized_alias,source) row FROM anime_alias WHERE anime_id IN (?,?) ORDER BY anime_id,normalized_alias)",
        "SELECT json_group_array(json(row)) FROM (SELECT json_array(e.subject_id,e.sort_number,e.ep_number) row FROM bangumi_episode_mapping e JOIN anime a ON a.bangumi_subject_id=e.subject_id WHERE a.id IN (?,?) ORDER BY e.subject_id,e.sort_number)",
        "SELECT json_group_array(json(row)) FROM (SELECT json_array(p.id,p.execution_state,p.target_path) row FROM organize_plan p JOIN media_file m ON m.id=p.media_file_id WHERE m.anime_id IN (?,?) ORDER BY p.id)"}) {
        auto stmt = prepare(db, sql);
        sqlite3_bind_int64(stmt.get(), 1, source); sqlite3_bind_int64(stmt.get(), 2, target);
        if (sqlite3_step(stmt.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
        result += text(stmt.get(), 0);
        if (result.size() > 4 * 1024 * 1024) throw AnimeBindingError("merge_too_large");
    }
    return result;
}
}

AnimeMergePreview SqliteMediaRepository::previewAnimeMerge(std::int64_t sourceId, std::int64_t subjectId) {
    if (sourceId <= 0 || subjectId <= 0) throw AnimeBindingError("invalid_id");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    const auto source = getAnime(sourceId);
    if (!source) throw AnimeBindingError("anime_not_found");
    if (source->locked) throw AnimeBindingError("anime_locked");
    auto owner = prepare(db, "SELECT id,display_title FROM anime WHERE bangumi_subject_id=?");
    sqlite3_bind_int64(owner.get(), 1, subjectId);
    if (sqlite3_step(owner.get()) != SQLITE_ROW) throw AnimeBindingError("merge_target_not_found");
    const auto targetId = sqlite3_column_int64(owner.get(), 0);
    if (sourceId == targetId) throw AnimeBindingError("merge_same_anime");
    checkRunning(db, sourceId, targetId);
    AnimeMergePreview preview;
    preview.sourceId = sourceId; preview.targetId = targetId; preview.subjectId = subjectId;
    preview.sourceTitle = source->displayTitle; preview.targetTitle = text(owner.get(), 1);
    preview.mediaCount = scalar(db, "SELECT COUNT(*) FROM media_file WHERE anime_id=? AND ? > 0", sourceId, targetId);
    auto aliasCount = prepare(db, "SELECT COUNT(*) FROM (SELECT normalized_alias FROM anime_alias WHERE anime_id=? UNION SELECT ? WHERE ?<>'' UNION SELECT ? WHERE ?<>'')");
    sqlite3_bind_int64(aliasCount.get(), 1, sourceId);
    const auto displayKey = normalizedTitleKey(source->displayTitle), originalKey = normalizedTitleKey(source->originalTitle);
    bind(aliasCount.get(), 2, displayKey); bind(aliasCount.get(), 3, displayKey);
    bind(aliasCount.get(), 4, originalKey); bind(aliasCount.get(), 5, originalKey);
    if (sqlite3_step(aliasCount.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    preview.aliasCount = sqlite3_column_int64(aliasCount.get(), 0);
    preview.duplicateEpisodeCount = scalar(db,
        "SELECT COUNT(*) FROM media_file s WHERE s.anime_id=? AND EXISTS(SELECT 1 FROM media_file t WHERE t.anime_id=? AND t.episode_type=s.episode_type AND t.episode_number=CASE WHEN s.episode_type='normal' AND s.episode_number_source!='manual' THEN COALESCE((SELECT e.ep_number FROM bangumi_episode_mapping e JOIN anime a ON a.bangumi_subject_id=e.subject_id WHERE a.id=t.anime_id AND e.sort_number=s.raw_episode_number),s.raw_episode_number) ELSE s.episode_number END)", sourceId, targetId);
    auto identity = prepare(db, "SELECT lower(hex(randomblob(16))),CAST(strftime('%s','now') AS INTEGER)+300");
    if (sqlite3_step(identity.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    preview.previewId = text(identity.get(), 0); preview.expiresAt = sqlite3_column_int64(identity.get(), 1);
    auto prune = prepare(db, "DELETE FROM anime_merge_preview WHERE expires_at<CAST(strftime('%s','now') AS INTEGER)"); done(db, prune.get());
    auto insert = prepare(db, "INSERT INTO anime_merge_preview(id,source_id,target_id,subject_id,snapshot,expires_at) VALUES(?,?,?,?,?,?)");
    bind(insert.get(), 1, preview.previewId);
    sqlite3_bind_int64(insert.get(), 2, sourceId); sqlite3_bind_int64(insert.get(), 3, targetId); sqlite3_bind_int64(insert.get(), 4, subjectId);
    bind(insert.get(), 5, snapshot(db, sourceId, targetId)); sqlite3_bind_int64(insert.get(), 6, preview.expiresAt); done(db, insert.get());
    tx.commit();
    return preview;
}

AnimeRecord SqliteMediaRepository::mergeAnime(std::int64_t sourceId, const std::string& previewId) {
    if (sourceId <= 0 || previewId.empty() || previewId.size() > 64) throw AnimeBindingError("invalid_merge_preview");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto saved = prepare(db, "SELECT target_id,subject_id,snapshot,completed FROM anime_merge_preview WHERE id=? AND source_id=? AND expires_at>CAST(strftime('%s','now') AS INTEGER)");
    bind(saved.get(), 1, previewId); sqlite3_bind_int64(saved.get(), 2, sourceId);
    if (sqlite3_step(saved.get()) != SQLITE_ROW) throw AnimeBindingError("merge_preview_expired");
    const auto targetId = sqlite3_column_int64(saved.get(), 0), subjectId = sqlite3_column_int64(saved.get(), 1);
    const auto expected = text(saved.get(), 2);
    const auto target = getAnime(targetId);
    if (!target || target->bangumiSubjectId != subjectId) throw AnimeBindingError("merge_target_changed");
    // 已完成凭证允许安全重试，绝不把重试当成第二次合并。
    if (sqlite3_column_int(saved.get(), 3)) { tx.commit(); return *target; }
    const auto source = getAnime(sourceId);
    if (!source) throw AnimeBindingError("anime_not_found");
    if (source->locked) throw AnimeBindingError("anime_locked");
    checkRunning(db, sourceId, targetId);
    if (snapshot(db, sourceId, targetId) != expected) throw AnimeBindingError("merge_preview_changed");
    auto aliases = prepare(db,
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) SELECT ?,normalized_alias,CASE WHEN source='user' THEN 'user' ELSE 'merged' END FROM anime_alias WHERE anime_id=? "
        "ON CONFLICT(anime_id,normalized_alias) DO UPDATE SET source=CASE WHEN excluded.source='user' OR anime_alias.source='user' THEN 'user' ELSE 'merged' END");
    sqlite3_bind_int64(aliases.get(), 1, targetId); sqlite3_bind_int64(aliases.get(), 2, sourceId); done(db, aliases.get());
    for (const auto& title : {source->displayTitle, source->originalTitle}) {
        const auto key = normalizedTitleKey(title);
        if (key.empty()) continue;
        auto alias = prepare(db, "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(?,?,'merged') ON CONFLICT(anime_id,normalized_alias) DO UPDATE SET source=CASE WHEN anime_alias.source='user' THEN 'user' ELSE 'merged' END");
        sqlite3_bind_int64(alias.get(), 1, targetId); bind(alias.get(), 2, key); done(db, alias.get());
    }
    auto expire = prepare(db, "UPDATE organize_plan SET execution_state='expired' WHERE execution_state='pending' AND media_file_id IN (SELECT id FROM media_file WHERE anime_id=?)");
    sqlite3_bind_int64(expire.get(), 1, sourceId); done(db, expire.get());
    auto media = prepare(db, "UPDATE media_file SET anime_id=?,title=?,season=?,updated_at=CURRENT_TIMESTAMP WHERE anime_id=?");
    sqlite3_bind_int64(media.get(), 1, targetId); bind(media.get(), 2, target->displayTitle); bind(media.get(), 3, target->season);
    sqlite3_bind_int64(media.get(), 4, sourceId); done(db, media.get());
    applyAnimeEpisodeMapping(targetId);
    auto removeAliases = prepare(db, "DELETE FROM anime_alias WHERE anime_id=?");
    sqlite3_bind_int64(removeAliases.get(), 1, sourceId); done(db, removeAliases.get());
    auto remove = prepare(db, "DELETE FROM anime WHERE id=?");
    sqlite3_bind_int64(remove.get(), 1, sourceId); done(db, remove.get());
    auto audit = prepare(db, "INSERT INTO audit_log(action,entity_type,entity_id,details_json) VALUES('anime_merged','anime',?,json_object('sourceId',?,'sourceTitle',?,'targetId',?,'targetTitle',?))");
    bind(audit.get(), 1, std::to_string(targetId)); sqlite3_bind_int64(audit.get(), 2, sourceId); bind(audit.get(), 3, source->displayTitle);
    sqlite3_bind_int64(audit.get(), 4, targetId); bind(audit.get(), 5, target->displayTitle); done(db, audit.get());
    auto complete = prepare(db, "UPDATE anime_merge_preview SET completed=1 WHERE id=?");
    bind(complete.get(), 1, previewId); done(db, complete.get());
    tx.commit();
    return *getAnime(targetId);
}
} // namespace anime_vault
