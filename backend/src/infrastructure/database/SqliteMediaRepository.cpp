#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace anime_vault {
namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
    return Statement(raw, sqlite3_finalize);
}
void bind(sqlite3_stmt* stmt, int index, const std::string& value) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        throw std::runtime_error("sqlite bind failed");
}
void done(sqlite3* db, sqlite3_stmt* stmt) {
    if (sqlite3_step(stmt) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
}
int dataVersion(sqlite3* db) {
    auto stmt = prepare(db, "PRAGMA data_version");
    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
        throw std::runtime_error(sqlite3_errmsg(db));
    return sqlite3_column_int(stmt.get(), 0);
}
std::string column(sqlite3_stmt* stmt, int index) {
    const auto* value = sqlite3_column_text(stmt, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}
ScanRecord scanRow(sqlite3_stmt* stmt) {
    return {sqlite3_column_int64(stmt, 0), column(stmt, 1), column(stmt, 2),
        sqlite3_column_int64(stmt, 3), sqlite3_column_int64(stmt, 4),
        sqlite3_column_int64(stmt, 5), column(stmt, 6)};
}
PlanRecord planRow(sqlite3_stmt* stmt) {
    return {sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1),
        column(stmt, 2), sqlite3_column_int64(stmt, 3), column(stmt, 4),
        column(stmt, 5), column(stmt, 6), column(stmt, 7), column(stmt, 8), column(stmt, 9)};
}
MediaRecord mediaRow(sqlite3_stmt* stmt) {
    MediaRecord record{sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1),
        column(stmt, 2), column(stmt, 3), column(stmt, 4), column(stmt, 5),
        sqlite3_column_int64(stmt, 6), column(stmt, 7), sqlite3_column_double(stmt, 8)};
    record.title = column(stmt, 9);
    record.season = column(stmt, 10);
    if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
        record.bangumiSubjectId = sqlite3_column_int64(stmt, 11);
    record.sourceModifiedAt = column(stmt, 12);
    record.origin = column(stmt, 13);
    if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
        record.animeId = sqlite3_column_int64(stmt, 14);
    record.parsedTitle = column(stmt, 15);
    record.libraryPath = column(stmt, 16);
    return record;
}
OrganizationJobRecord jobRow(sqlite3_stmt* stmt) {
    return {sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1), column(stmt, 2),
            column(stmt, 3), column(stmt, 4), column(stmt, 5), sqlite3_column_int64(stmt, 6)};
}
void exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : "sqlite transaction failed";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
struct Transaction {
    sqlite3* db;
    bool committed{};
    explicit Transaction(sqlite3* value) : db(value) { exec(db, "BEGIN IMMEDIATE"); }
    ~Transaction() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { exec(db, "COMMIT"); committed = true; }
};
std::optional<OrganizationJobRecord> findJobByKey(sqlite3* db, const std::string& key) {
    auto stmt = prepare(db, "SELECT id,plan_id,idempotency_key,status,result_path,failure_details,result_bytes FROM organize_job WHERE idempotency_key=?");
    bind(stmt.get(), 1, key);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return jobRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}
void audit(sqlite3* db, std::int64_t jobId, const char* action, const std::string& code) {
    auto stmt = prepare(db, "INSERT INTO audit_log(action,entity_type,entity_id,details_json) VALUES(?,'organize_job',?,?)");
    bind(stmt.get(), 1, action);
    bind(stmt.get(), 2, std::to_string(jobId));
    bind(stmt.get(), 3, "{\"code\":\"" + code + "\"}");
    done(db, stmt.get());
}
AnimeRecord animeRow(sqlite3_stmt* stmt) {
    AnimeRecord record;
    record.id = sqlite3_column_int64(stmt, 0);
    record.displayTitle = column(stmt, 1);
    record.originalTitle = column(stmt, 2);
    record.season = column(stmt, 3);
    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) record.year = sqlite3_column_int(stmt, 4);
    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
        record.bangumiSubjectId = sqlite3_column_int64(stmt, 5);
    record.coverUrl = column(stmt, 6);
    record.locked = sqlite3_column_int(stmt, 7) != 0;
    return record;
}
void addTitleIndexEntry(std::unordered_map<std::string, std::vector<AnimeRecord>>& index,
                        const std::string& key, const AnimeRecord& anime) {
    if (key.empty()) return;
    auto& records = index[key];
    if (std::none_of(records.begin(), records.end(), [&anime](const auto& item) {
            return item.id == anime.id;
        })) records.push_back(anime);
}
void fillAnime(sqlite3* db, AnimeRecord& record, std::int64_t mediaOffset, int mediaLimit) {
    auto aliases = prepare(db, "SELECT normalized_alias FROM anime_alias WHERE anime_id=? ORDER BY normalized_alias LIMIT 100");
    sqlite3_bind_int64(aliases.get(), 1, record.id);
    int rc;
    while ((rc = sqlite3_step(aliases.get())) == SQLITE_ROW)
        record.aliases.push_back(column(aliases.get(), 0));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    auto media = prepare(db, "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path FROM media_file m JOIN anime a ON a.id=m.anime_id WHERE a.id=? AND m.status!='missing' ORDER BY m.id LIMIT ? OFFSET ?");
    sqlite3_bind_int64(media.get(), 1, record.id);
    sqlite3_bind_int(media.get(), 2, mediaLimit + 1);
    sqlite3_bind_int64(media.get(), 3, mediaOffset);
    while ((rc = sqlite3_step(media.get())) == SQLITE_ROW)
        record.media.push_back(mediaRow(media.get()));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    if (record.media.size() > static_cast<std::size_t>(mediaLimit)) {
        record.media.resize(mediaLimit);
        record.nextMediaOffset = mediaOffset + mediaLimit;
    }
}
}

std::int64_t SqliteMediaRepository::createScan(const ScanRecord& record) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "INSERT INTO scan_job(source,status,discovered_count,processed_count,error_count,error_summary) VALUES(?,?,?,?,?,?)");
    bind(stmt.get(), 1, record.source); bind(stmt.get(), 2, record.status);
    sqlite3_bind_int64(stmt.get(), 3, record.discoveredCount);
    sqlite3_bind_int64(stmt.get(), 4, record.processedCount);
    sqlite3_bind_int64(stmt.get(), 5, record.errorCount);
    bind(stmt.get(), 6, record.errorSummary);
    done(db, stmt.get());
    return sqlite3_last_insert_rowid(db);
}

std::optional<ScanRecord> SqliteMediaRepository::getScan(std::int64_t id) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,source,status,discovered_count,processed_count,error_count,error_summary FROM scan_job WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return scanRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}

void SqliteMediaRepository::updateScan(const ScanRecord& record) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "UPDATE scan_job SET status=?,discovered_count=?,processed_count=?,error_count=?,error_summary=?,updated_at=CURRENT_TIMESTAMP WHERE id=?");
    bind(stmt.get(), 1, record.status);
    sqlite3_bind_int64(stmt.get(), 2, record.discoveredCount);
    sqlite3_bind_int64(stmt.get(), 3, record.processedCount);
    sqlite3_bind_int64(stmt.get(), 4, record.errorCount);
    bind(stmt.get(), 5, record.errorSummary);
    sqlite3_bind_int64(stmt.get(), 6, record.id);
    done(db, stmt.get());
    if (sqlite3_changes(db) == 0) throw std::runtime_error("scan not found");
}

std::int64_t SqliteMediaRepository::insertMedia(const MediaRecord& record) {
    std::lock_guard lock(database_.mutex());
    if (record.origin != "qb_download" && record.origin != "external_import")
        throw std::invalid_argument("invalid media origin");
    auto* db = database_.handle();
    const auto beforeChanges = sqlite3_total_changes64(db);
    auto stmt = prepare(db, "INSERT INTO media_file(scan_id,source_path,filename,episode_number,episode_type,size_bytes,status,confidence,source_modified_at,origin,parsed_title) VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(source_path) DO UPDATE SET scan_id=excluded.scan_id,filename=excluded.filename,size_bytes=excluded.size_bytes,source_modified_at=excluded.source_modified_at,status=CASE WHEN media_file.status='missing' AND media_file.library_path IS NULL THEN 'inbox' ELSE media_file.status END,updated_at=CURRENT_TIMESTAMP WHERE media_file.origin=excluded.origin");
    sqlite3_bind_int64(stmt.get(), 1, record.scanId);
    bind(stmt.get(), 2, record.sourcePath); bind(stmt.get(), 3, record.filename);
    bind(stmt.get(), 4, record.episodeNumber); bind(stmt.get(), 5, record.episodeType);
    sqlite3_bind_int64(stmt.get(), 6, record.sizeBytes);
    bind(stmt.get(), 7, record.status);
    sqlite3_bind_double(stmt.get(), 8, record.confidence);
    bind(stmt.get(), 9, record.sourceModifiedAt);
    bind(stmt.get(), 10, record.origin);
    bind(stmt.get(), 11, record.parsedTitle);
    done(db, stmt.get());
    if (sqlite3_changes(db) == 0)
        throw std::invalid_argument("media origin conflicts with existing source path");
    auto lookup = prepare(db, "SELECT id FROM media_file WHERE source_path=?");
    bind(lookup.get(), 1, record.sourcePath);
    if (sqlite3_step(lookup.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    const auto id = sqlite3_column_int64(lookup.get(), 0);
    if (titleIndexChanges_ == beforeChanges)
        titleIndexChanges_ = sqlite3_total_changes64(db);
    return id;
}

void SqliteMediaRepository::markMissingMedia(const std::string& origin,
                                              const std::vector<std::string>& observedPaths) {
    if (origin != "qb_download" && origin != "external_import")
        throw std::invalid_argument("invalid media origin");
    const std::unordered_set<std::string> observed(observedPaths.begin(), observedPaths.end());
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto candidates = prepare(db, "SELECT id,source_path FROM media_file "
        "WHERE origin=? AND status='inbox' AND library_path IS NULL");
    bind(candidates.get(), 1, origin);
    auto mark = prepare(db, "UPDATE media_file SET status='missing',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND status='inbox' AND library_path IS NULL");
    int rc;
    while ((rc = sqlite3_step(candidates.get())) == SQLITE_ROW) {
        if (observed.contains(column(candidates.get(), 1))) continue;
        sqlite3_reset(mark.get());
        sqlite3_clear_bindings(mark.get());
        sqlite3_bind_int64(mark.get(), 1, sqlite3_column_int64(candidates.get(), 0));
        done(db, mark.get());
    }
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    tx.commit();
}

std::optional<MediaRecord> SqliteMediaRepository::getMedia(std::int64_t id) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path FROM media_file m LEFT JOIN anime a ON a.id=m.anime_id WHERE m.id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return mediaRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}

void SqliteMediaRepository::updateMediaParse(std::int64_t id, const std::string& episodeNumber,
                                             const std::string& episodeType, double confidence) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "UPDATE media_file SET episode_number=?,episode_type=?,confidence=?,updated_at=CURRENT_TIMESTAMP WHERE id=?");
    bind(stmt.get(), 1, episodeNumber); bind(stmt.get(), 2, episodeType);
    sqlite3_bind_double(stmt.get(), 3, confidence);
    sqlite3_bind_int64(stmt.get(), 4, id);
    done(db, stmt.get());
    if (sqlite3_changes(db) == 0) throw std::runtime_error("media not found");
}

void SqliteMediaRepository::updateMediaCorrection(
    std::int64_t id, const MediaCorrection& correction, MediaCorrectionIntent intent) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    const auto existingMedia = getMedia(id);
    if (!existingMedia) throw std::runtime_error("media not found");
    Transaction tx(db);
    const auto changesBeforeAnimeResolution = sqlite3_total_changes64(db);
    std::int64_t animeId{};
    std::string priorDisplayTitle;
    if (existingMedia->animeId) {
        auto previous = prepare(db, "SELECT display_title FROM anime WHERE id=?");
        sqlite3_bind_int64(previous.get(), 1, *existingMedia->animeId);
        if (sqlite3_step(previous.get()) == SQLITE_ROW) priorDisplayTitle = column(previous.get(), 0);
    }
    if (correction.bangumiSubjectId) {
        auto anime = prepare(db, "INSERT INTO anime(display_title,season,bangumi_subject_id) VALUES(?,?,?) ON CONFLICT(bangumi_subject_id) DO NOTHING");
        bind(anime.get(), 1, correction.title);
        bind(anime.get(), 2, correction.season);
        sqlite3_bind_int64(anime.get(), 3, *correction.bangumiSubjectId);
        done(db, anime.get());
        auto lookup = prepare(db, "SELECT id FROM anime WHERE bangumi_subject_id=?");
        sqlite3_bind_int64(lookup.get(), 1, *correction.bangumiSubjectId);
        if (sqlite3_step(lookup.get()) != SQLITE_ROW) throw std::runtime_error("anime not found");
        animeId = sqlite3_column_int64(lookup.get(), 0);
    } else {
        if (intent == MediaCorrectionIntent::userConfirmed && existingMedia->animeId) {
            animeId = *existingMedia->animeId;
        }
        auto current = prepare(db, "SELECT a.id FROM media_file m JOIN anime a ON a.id=m.anime_id WHERE m.id=? AND a.display_title=? AND COALESCE(a.season,'')=?");
        if (!animeId) {
            sqlite3_bind_int64(current.get(), 1, id);
            bind(current.get(), 2, correction.title);
            bind(current.get(), 3, correction.season);
            if (sqlite3_step(current.get()) == SQLITE_ROW) animeId = sqlite3_column_int64(current.get(), 0);
        }
        if (!animeId) {
            auto bound = prepare(db, "SELECT a.id FROM media_file m JOIN anime a ON a.id=m.anime_id WHERE m.id=? AND a.bangumi_subject_id IS NOT NULL");
            sqlite3_bind_int64(bound.get(), 1, id);
            if (sqlite3_step(bound.get()) == SQLITE_ROW) animeId = sqlite3_column_int64(bound.get(), 0);
        }
        if (!animeId) {
            if (const auto local = findAnimeByTitle(correction.title, correction.season))
                animeId = local->id;
        }
        if (!animeId) {
            auto anime = prepare(db, "INSERT INTO anime(display_title,season) VALUES(?,?)");
            bind(anime.get(), 1, correction.title);
            bind(anime.get(), 2, correction.season);
            done(db, anime.get());
            animeId = sqlite3_last_insert_rowid(db);
        }
    }
    const bool preserveTitleIndex = intent == MediaCorrectionIntent::automaticScan &&
        sqlite3_total_changes64(db) == changesBeforeAnimeResolution &&
        titleIndexChanges_ == sqlite3_total_changes64(db) &&
        titleIndexDataVersion_ == dataVersion(db);
    auto stmt = prepare(db, "UPDATE media_file SET title=?,season=?,episode_number=?,episode_type=?,confidence=?,anime_id=?,updated_at=CURRENT_TIMESTAMP WHERE id=?");
    bind(stmt.get(), 1, correction.title);
    bind(stmt.get(), 2, correction.season);
    bind(stmt.get(), 3, correction.episodeNumber);
    bind(stmt.get(), 4, correction.episodeType);
    sqlite3_bind_double(stmt.get(), 5, correction.confidence);
    sqlite3_bind_int64(stmt.get(), 6, animeId);
    sqlite3_bind_int64(stmt.get(), 7, id);
    done(db, stmt.get());
    if (sqlite3_changes(db) == 0) throw std::runtime_error("media not found");
    if (intent == MediaCorrectionIntent::userConfirmed) {
        auto rename = prepare(db, "UPDATE anime SET display_title=?,updated_at=CURRENT_TIMESTAMP WHERE id=?");
        bind(rename.get(), 1, correction.title);
        sqlite3_bind_int64(rename.get(), 2, animeId);
        done(db, rename.get());
        auto insert = prepare(db, "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(?,?,'user') ON CONFLICT(anime_id,normalized_alias) DO UPDATE SET source='user'");
        std::vector<std::string> aliases{correction.title};
        if (!existingMedia->animeId || *existingMedia->animeId == animeId) {
            aliases.push_back(existingMedia->parsedTitle);
        }
        if (existingMedia->animeId && *existingMedia->animeId == animeId)
            aliases.push_back(priorDisplayTitle);
        for (const auto& title : aliases) {
            const auto alias = normalizedTitleKey(title);
            if (alias.empty()) continue;
            sqlite3_bind_int64(insert.get(), 1, animeId);
            bind(insert.get(), 2, alias);
            done(db, insert.get());
            sqlite3_reset(insert.get());
            sqlite3_clear_bindings(insert.get());
        }
    }
    tx.commit();
    if (preserveTitleIndex) {
        titleIndexChanges_ = sqlite3_total_changes64(db);
    } else {
        titleIndexChanges_ = -1;
        titleIndexDataVersion_ = -1;
        titleIndex_.clear();
    }
}

std::int64_t SqliteMediaRepository::insertPlan(const PlanRecord& record) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "INSERT INTO organize_plan(media_file_id,source_path,source_size,source_modified_at,target_path,operation,expires_at,execution_state,idempotency_key) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(idempotency_key) DO NOTHING");
    sqlite3_bind_int64(stmt.get(), 1, record.mediaFileId);
    bind(stmt.get(), 2, record.sourcePath);
    sqlite3_bind_int64(stmt.get(), 3, record.sourceSize);
    bind(stmt.get(), 4, record.sourceModifiedAt); bind(stmt.get(), 5, record.targetPath);
    bind(stmt.get(), 6, record.operation); bind(stmt.get(), 7, record.expiresAt);
    bind(stmt.get(), 8, record.executionState); bind(stmt.get(), 9, record.idempotencyKey);
    done(db, stmt.get());
    const auto plan = findPlanByIdempotencyKey(record.idempotencyKey);
    if (!plan) throw std::runtime_error("plan insert failed");
    return plan->id;
}

std::optional<PlanRecord> SqliteMediaRepository::getPlan(std::int64_t id) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,media_file_id,source_path,source_size,source_modified_at,target_path,operation,expires_at,execution_state,idempotency_key FROM organize_plan WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return planRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}

std::optional<PlanRecord> SqliteMediaRepository::findPlanByIdempotencyKey(const std::string& key) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,media_file_id,source_path,source_size,source_modified_at,target_path,operation,expires_at,execution_state,idempotency_key FROM organize_plan WHERE idempotency_key=?");
    bind(stmt.get(), 1, key);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return planRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}

OrganizationJobRecord SqliteMediaRepository::claimOrganization(std::int64_t planId,
                                                               const std::string& key) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    if (const auto previous = findJobByKey(db, key)) {
        if (previous->planId != planId) throw OrganizationClaimError("idempotency_key_reused");
        tx.commit();
        return *previous;
    }
    auto update = prepare(db, "UPDATE organize_plan SET execution_state='claimed',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND execution_state='pending' AND expires_at>strftime('%Y-%m-%dT%H:%M:%SZ','now') "
        "AND EXISTS (SELECT 1 FROM media_file m WHERE m.id=organize_plan.media_file_id AND m.library_path IS NULL) "
        "AND NOT EXISTS (SELECT 1 FROM organize_plan p JOIN organize_job j ON j.plan_id=p.id "
        "WHERE p.media_file_id=organize_plan.media_file_id AND j.status IN ('running','completed'))");
    sqlite3_bind_int64(update.get(), 1, planId);
    done(db, update.get());
    if (sqlite3_changes(db) != 1) {
        auto plan = prepare(db, "SELECT execution_state,expires_at<=strftime('%Y-%m-%dT%H:%M:%SZ','now') "
            "FROM organize_plan WHERE id=?");
        sqlite3_bind_int64(plan.get(), 1, planId);
        if (sqlite3_step(plan.get()) != SQLITE_ROW) throw OrganizationClaimError("plan_not_found");
        const auto state = column(plan.get(), 0);
        const bool expired = sqlite3_column_int(plan.get(), 1) != 0;
        if (state.rfind("conflict:", 0) == 0) throw OrganizationClaimError("plan_conflict");
        if (state != "pending") throw OrganizationClaimError("plan_already_claimed");
        if (expired) throw OrganizationClaimError("plan_expired");
        throw OrganizationClaimError("plan_already_claimed");
    }
    auto insert = prepare(db, "INSERT INTO organize_job(plan_id,status,idempotency_key) VALUES(?,'running',?)");
    sqlite3_bind_int64(insert.get(), 1, planId);
    bind(insert.get(), 2, key);
    done(db, insert.get());
    const auto jobId = sqlite3_last_insert_rowid(db);
    audit(db, jobId, "organization_claimed", "claimed");
    tx.commit();
    return {jobId, planId, key, "running", "", "", 0, true};
}

std::optional<OrganizationJobRecord> SqliteMediaRepository::findOrganizationJobByKey(
    const std::string& key) const {
    std::lock_guard lock(database_.mutex());
    return findJobByKey(database_.handle(), key);
}

std::optional<OrganizationJobRecord> SqliteMediaRepository::getOrganizationJob(std::int64_t id) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,plan_id,idempotency_key,status,result_path,failure_details,result_bytes FROM organize_job WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return jobRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}

void SqliteMediaRepository::completeOrganization(std::int64_t jobId,
                                                 const std::string& resultPath,
                                                 std::int64_t bytes) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto media = prepare(db, "UPDATE media_file SET library_path=?,link_mode=(SELECT operation FROM organize_plan p "
        "JOIN organize_job j ON j.plan_id=p.id WHERE j.id=?),status='organized',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=(SELECT p.media_file_id FROM organize_plan p JOIN organize_job j ON j.plan_id=p.id WHERE j.id=? "
        "AND j.status='running') AND library_path IS NULL");
    bind(media.get(), 1, resultPath);
    sqlite3_bind_int64(media.get(), 2, jobId);
    sqlite3_bind_int64(media.get(), 3, jobId);
    done(db, media.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    auto job = prepare(db, "UPDATE organize_job SET status='completed',result_path=?,result_bytes=?,updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND status='running'");
    bind(job.get(), 1, resultPath);
    sqlite3_bind_int64(job.get(), 2, bytes);
    sqlite3_bind_int64(job.get(), 3, jobId);
    done(db, job.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    auto plan = prepare(db, "UPDATE organize_plan SET execution_state='completed',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=(SELECT plan_id FROM organize_job WHERE id=?) AND execution_state='claimed'");
    sqlite3_bind_int64(plan.get(), 1, jobId);
    done(db, plan.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    audit(db, jobId, "organization_completed", "completed");
    tx.commit();
}

void SqliteMediaRepository::failOrganization(std::int64_t jobId, const std::string& code) {
    std::lock_guard lock(database_.mutex());
    if (code.empty() || code.size() > 64 ||
        !std::all_of(code.begin(), code.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
        })) throw std::invalid_argument("invalid failure code");
    auto* db = database_.handle();
    Transaction tx(db);
    auto job = prepare(db, "UPDATE organize_job SET status='failed',failure_details=?,updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND status='running'");
    bind(job.get(), 1, code);
    sqlite3_bind_int64(job.get(), 2, jobId);
    done(db, job.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    auto plan = prepare(db, "UPDATE organize_plan SET execution_state='failed',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=(SELECT plan_id FROM organize_job WHERE id=?) AND execution_state='claimed'");
    sqlite3_bind_int64(plan.get(), 1, jobId);
    done(db, plan.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    audit(db, jobId, "organization_failed", code);
    tx.commit();
}

std::vector<ScanRecord> SqliteMediaRepository::listScans() const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,source,status,discovered_count,processed_count,error_count,error_summary FROM scan_job ORDER BY id");
    std::vector<ScanRecord> result;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
        result.push_back(scanRow(stmt.get()));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    return result;
}

ScanPage SqliteMediaRepository::listScanPage(std::int64_t offset, int limit) const {
    if (offset < 0 || offset > 1'000'000 || limit < 1 || limit > 100)
        throw std::invalid_argument("invalid scan page");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,source,status,discovered_count,processed_count,error_count,error_summary FROM scan_job ORDER BY id DESC LIMIT ? OFFSET ?");
    sqlite3_bind_int(stmt.get(), 1, limit + 1);
    sqlite3_bind_int64(stmt.get(), 2, offset);
    ScanPage page;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) page.items.push_back(scanRow(stmt.get()));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    if (page.items.size() > static_cast<std::size_t>(limit)) {
        page.items.resize(limit);
        page.nextOffset = offset + limit;
    }
    return page;
}

AuditPage SqliteMediaRepository::listAuditPage(std::int64_t offset, int limit,
                                                std::optional<std::int64_t> animeId) const {
    if (offset < 0 || offset > 1'000'000 || limit < 1 || limit > 100 ||
        (animeId && *animeId <= 0)) throw std::invalid_argument("invalid audit page");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, animeId
        ? "SELECT id,action,entity_type,entity_id,created_at FROM audit_log WHERE action='anime_bound' AND entity_type='anime' AND entity_id=? ORDER BY id DESC LIMIT ? OFFSET ?"
        : "SELECT id,action,entity_type,entity_id,created_at FROM audit_log ORDER BY id DESC LIMIT ? OFFSET ?");
    int parameter = 1;
    if (animeId) bind(stmt.get(), parameter++, std::to_string(*animeId));
    sqlite3_bind_int(stmt.get(), parameter++, limit + 1);
    sqlite3_bind_int64(stmt.get(), parameter, offset);
    AuditPage page;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
        page.items.push_back({sqlite3_column_int64(stmt.get(), 0), column(stmt.get(), 1),
                              column(stmt.get(), 2), column(stmt.get(), 3), column(stmt.get(), 4)});
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    if (page.items.size() > static_cast<std::size_t>(limit)) {
        page.items.resize(limit);
        page.nextOffset = offset + limit;
    }
    return page;
}

UiPreferences SqliteMediaRepository::getUiPreferences() const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT key,json_extract(value_json,'$') FROM setting WHERE key IN ('preferredOperation','scanIntervalSeconds','mpvExecutable','qbWebUiUrl')");
    UiPreferences preferences;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
        const auto key = column(stmt.get(), 0);
        if (key == "preferredOperation") preferences.preferredOperation = column(stmt.get(), 1);
        else if (key == "scanIntervalSeconds") preferences.scanIntervalSeconds = sqlite3_column_int(stmt.get(), 1);
        else if (key == "mpvExecutable") preferences.mpvExecutable = column(stmt.get(), 1);
        else if (key == "qbWebUiUrl") preferences.qbWebUiUrl = column(stmt.get(), 1);
    }
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    return preferences;
}

void SqliteMediaRepository::putUiPreferences(const UiPreferences& preferences) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto stmt = prepare(db, "INSERT INTO setting(key,value_json) VALUES(?,json_quote(?)) ON CONFLICT(key) DO UPDATE SET value_json=excluded.value_json,updated_at=CURRENT_TIMESTAMP");
    for (const auto& [key, value] : {std::pair{"preferredOperation", preferences.preferredOperation},
                                    {"scanIntervalSeconds", std::to_string(preferences.scanIntervalSeconds)},
                                    {"mpvExecutable", preferences.mpvExecutable},
                                    {"qbWebUiUrl", preferences.qbWebUiUrl}}) {
        sqlite3_reset(stmt.get());
        sqlite3_clear_bindings(stmt.get());
        bind(stmt.get(), 1, key);
        bind(stmt.get(), 2, value);
        done(db, stmt.get());
    }
    tx.commit();
}

std::vector<MediaRecord> SqliteMediaRepository::listInbox() const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path FROM media_file m LEFT JOIN anime a ON a.id=m.anime_id WHERE m.status='inbox' ORDER BY m.id");
    std::vector<MediaRecord> result;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
        result.push_back(mediaRow(stmt.get()));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    return result;
}

std::optional<std::int64_t> boundedInboxNextOffset(std::int64_t offset, int limit, bool hasMore) {
    if (!hasMore || offset + limit > 1'000'000) return std::nullopt;
    return offset + limit;
}

InboxPage SqliteMediaRepository::listInboxPage(std::int64_t offset, int limit,
                                               std::optional<std::string> origin) const {
    if (offset < 0 || offset > 1'000'000 || limit < 1 || limit > 100)
        throw std::invalid_argument("invalid inbox page");
    if (origin && *origin != "qb_download" && *origin != "external_import")
        throw std::invalid_argument("invalid inbox origin");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    const bool filtered = origin.has_value();
    auto count = prepare(db, filtered
        ? "SELECT COUNT(*) FROM media_file WHERE status='inbox' AND origin=?"
        : "SELECT COUNT(*) FROM media_file WHERE status='inbox'");
    if (filtered) bind(count.get(), 1, *origin);
    if (sqlite3_step(count.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    InboxPage page;
    page.total = sqlite3_column_int64(count.get(), 0);
    auto stmt = prepare(db, filtered
        ? "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path FROM media_file m LEFT JOIN anime a ON a.id=m.anime_id WHERE m.status='inbox' AND m.origin=? ORDER BY m.id LIMIT ? OFFSET ?"
        : "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path FROM media_file m LEFT JOIN anime a ON a.id=m.anime_id WHERE m.status='inbox' ORDER BY m.id LIMIT ? OFFSET ?");
    // Count and page use the same filter so a busy source cannot hide the other source.
    if (filtered) bind(stmt.get(), 1, *origin);
    const int firstPageParameter = filtered ? 2 : 1;
    sqlite3_bind_int(stmt.get(), firstPageParameter, limit + 1);
    sqlite3_bind_int64(stmt.get(), firstPageParameter + 1, offset);
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) page.items.push_back(mediaRow(stmt.get()));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    if (page.items.size() > static_cast<std::size_t>(limit)) {
        page.items.resize(limit);
        page.nextOffset = boundedInboxNextOffset(offset, limit, true);
    }
    return page;
}

std::optional<BangumiCacheRecord> SqliteMediaRepository::getBangumiCache(const std::string& key) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT query_key,response_json,expires_at,retry_count,retry_after FROM bangumi_cache WHERE query_key=?");
    bind(stmt.get(), 1, key);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    if (rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    BangumiCacheRecord record{column(stmt.get(), 0), column(stmt.get(), 1),
                              std::stoll(column(stmt.get(), 2)), sqlite3_column_int(stmt.get(), 3)};
    if (sqlite3_column_type(stmt.get(), 4) != SQLITE_NULL)
        record.retryAfter = std::stoll(column(stmt.get(), 4));
    return record;
}

void SqliteMediaRepository::putBangumiCache(const BangumiCacheRecord& record) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "INSERT INTO bangumi_cache(query_key,response_json,expires_at,retry_count,retry_after) "
        "VALUES(?,?,?,?,?) ON CONFLICT(query_key) DO UPDATE SET response_json=excluded.response_json,"
        "expires_at=excluded.expires_at,retry_count=excluded.retry_count,retry_after=excluded.retry_after,"
        "updated_at=CURRENT_TIMESTAMP");
    bind(stmt.get(), 1, record.queryKey);
    bind(stmt.get(), 2, record.responseJson);
    bind(stmt.get(), 3, std::to_string(record.expiresAt));
    sqlite3_bind_int(stmt.get(), 4, record.retryCount);
    if (record.retryAfter) bind(stmt.get(), 5, std::to_string(*record.retryAfter));
    else sqlite3_bind_null(stmt.get(), 5);
    done(db, stmt.get());
}

bool SqliteMediaRepository::putBangumiFailureIfStale(const BangumiCacheRecord& record,
                                                     std::int64_t requestStartedAt) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "INSERT INTO bangumi_cache(query_key,response_json,expires_at,retry_count,retry_after) "
        "VALUES(?,?,?,?,?) ON CONFLICT(query_key) DO UPDATE SET response_json=excluded.response_json,"
        "expires_at=excluded.expires_at,retry_count=excluded.retry_count,retry_after=excluded.retry_after,"
        "updated_at=CURRENT_TIMESTAMP WHERE CAST(bangumi_cache.expires_at AS INTEGER)<=?");
    bind(stmt.get(), 1, record.queryKey);
    bind(stmt.get(), 2, record.responseJson);
    bind(stmt.get(), 3, std::to_string(record.expiresAt));
    sqlite3_bind_int(stmt.get(), 4, record.retryCount);
    if (record.retryAfter) bind(stmt.get(), 5, std::to_string(*record.retryAfter));
    else sqlite3_bind_null(stmt.get(), 5);
    sqlite3_bind_int64(stmt.get(), 6, requestStartedAt);
    done(db, stmt.get());
    return sqlite3_changes(db) == 1;
}

std::vector<AnimeRecord> SqliteMediaRepository::listAnime() const {
    return listAnimePage(0, 200).items;
}

AnimePage SqliteMediaRepository::listAnimePage(std::int64_t offset, int limit) const {
    if (offset < 0 || offset > 1'000'000 || limit < 1 || limit > 200)
        throw std::invalid_argument("invalid anime page");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT a.id,a.display_title,a.original_title,a.season,a.year,a.bangumi_subject_id,a.cover_url,a.locked FROM anime a WHERE NOT EXISTS (SELECT 1 FROM media_file m WHERE m.anime_id=a.id) OR EXISTS (SELECT 1 FROM media_file m WHERE m.anime_id=a.id AND m.status!='missing') ORDER BY a.id DESC LIMIT ? OFFSET ?");
    sqlite3_bind_int(stmt.get(), 1, limit + 1);
    sqlite3_bind_int64(stmt.get(), 2, offset);
    AnimePage page;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) page.items.push_back(animeRow(stmt.get()));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    if (page.items.size() > static_cast<std::size_t>(limit)) {
        page.items.resize(limit);
        page.nextOffset = offset + limit;
    }
    return page;
}

std::optional<AnimeRecord> SqliteMediaRepository::getAnime(std::int64_t id,
                                                           std::int64_t mediaOffset,
                                                           int mediaLimit) const {
    if (mediaOffset < 0 || mediaOffset > 1'000'000 || mediaLimit < 1 || mediaLimit > 200)
        throw std::invalid_argument("invalid anime media page");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,display_title,original_title,season,year,bangumi_subject_id,cover_url,locked FROM anime WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    if (rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    auto record = animeRow(stmt.get());
    fillAnime(db, record, mediaOffset, mediaLimit);
    return record;
}

AnimeRecord SqliteMediaRepository::bindAnime(std::int64_t id, const BangumiSubject& subject) {
    if (id <= 0) throw AnimeBindingError("invalid_id");
    if (subject.id <= 0 || subject.type != 2 || subject.name.empty() ||
        subject.name.size() > 500 || subject.nameCn.size() > 500 ||
        subject.coverUrl.size() > 2048 || subject.date.size() > 32)
        throw AnimeBindingError("invalid_subject");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto current = prepare(db, "SELECT bangumi_subject_id,locked FROM anime WHERE id=?");
    sqlite3_bind_int64(current.get(), 1, id);
    if (sqlite3_step(current.get()) != SQLITE_ROW) throw AnimeBindingError("anime_not_found");
    const bool hadBinding = sqlite3_column_type(current.get(), 0) != SQLITE_NULL;
    const bool same = hadBinding &&
        sqlite3_column_int64(current.get(), 0) == subject.id;
    if (sqlite3_column_int(current.get(), 1) && !same) throw AnimeBindingError("anime_locked");
    auto owner = prepare(db, "SELECT id FROM anime WHERE bangumi_subject_id=? AND id<>?");
    sqlite3_bind_int64(owner.get(), 1, subject.id);
    sqlite3_bind_int64(owner.get(), 2, id);
    if (sqlite3_step(owner.get()) == SQLITE_ROW) throw AnimeBindingError("bangumi_subject_in_use");
    if (!same || !sqlite3_column_int(current.get(), 1)) {
        const bool rebinding = hadBinding && !same;
        auto update = prepare(db, rebinding
            ? "UPDATE anime SET bangumi_subject_id=?,original_title=?,cover_url=?,year=?,updated_at=CURRENT_TIMESTAMP WHERE id=?"
            : "UPDATE anime SET bangumi_subject_id=?,original_title=?,cover_url=CASE WHEN cover_url IS NULL OR cover_url='' THEN ? ELSE cover_url END,year=COALESCE(year,?),updated_at=CURRENT_TIMESTAMP WHERE id=?");
        sqlite3_bind_int64(update.get(), 1, subject.id);
        bind(update.get(), 2, subject.name);
        bind(update.get(), 3, subject.coverUrl);
        int year{};
        if (subject.date.size() >= 4 && std::all_of(subject.date.begin(), subject.date.begin() + 4,
                [](unsigned char c) { return std::isdigit(c); }))
            year = std::stoi(subject.date.substr(0, 4));
        if (year >= 1900 && year <= 2100) sqlite3_bind_int(update.get(), 4, year);
        else sqlite3_bind_null(update.get(), 4);
        sqlite3_bind_int64(update.get(), 5, id);
        if (sqlite3_step(update.get()) != SQLITE_DONE) {
            if (sqlite3_errcode(db) == SQLITE_CONSTRAINT) throw AnimeBindingError("bangumi_subject_in_use");
            throw std::runtime_error(sqlite3_errmsg(db));
        }
        auto removeAliases = prepare(db,
            "DELETE FROM anime_alias WHERE anime_id=? AND source='bangumi'");
        sqlite3_bind_int64(removeAliases.get(), 1, id);
        done(db, removeAliases.get());
        for (const auto& name : {subject.name, subject.nameCn}) {
            const auto alias = normalizedTitleKey(name);
            if (alias.empty()) continue;
            auto insert = prepare(db, "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(?,?,'bangumi') ON CONFLICT(anime_id,normalized_alias) DO NOTHING");
            sqlite3_bind_int64(insert.get(), 1, id);
            bind(insert.get(), 2, alias);
            done(db, insert.get());
        }
        auto auditEntry = prepare(db, "INSERT INTO audit_log(action,entity_type,entity_id,details_json) VALUES('anime_bound','anime',?,'{}')");
        bind(auditEntry.get(), 1, std::to_string(id));
        done(db, auditEntry.get());
    }
    tx.commit();
    return *getAnime(id);
}

bool SqliteMediaRepository::updateCoverIfBound(std::int64_t animeId, std::int64_t subjectId,
                                                const std::string& localUrl) {
    if (animeId <= 0 || subjectId <= 0 || localUrl.empty() || localUrl.size() > 2048 ||
        !localUrl.starts_with("/api/covers/")) return false;
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto update = prepare(db,
        "UPDATE anime SET cover_url=?,updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND bangumi_subject_id=?");
    bind(update.get(), 1, localUrl);
    sqlite3_bind_int64(update.get(), 2, animeId);
    sqlite3_bind_int64(update.get(), 3, subjectId);
    done(db, update.get());
    const bool updated = sqlite3_changes(db) == 1;
    if (updated) {
        auto audit = prepare(db,
            "INSERT INTO audit_log(action,entity_type,entity_id,details_json) "
            "VALUES('cover_cached','anime',?,'{}')");
        bind(audit.get(), 1, std::to_string(animeId));
        done(db, audit.get());
    }
    tx.commit();
    return updated;
}

bool SqliteMediaRepository::applyMikanAlias(const std::string& alias,
                                            const std::string& canonicalTitle) {
    if (alias.empty() || canonicalTitle.empty() || alias.size() > 500 ||
        canonicalTitle.size() > 500) return false;
    const auto key = normalizedTitleKey(alias);
    if (key.empty() || key == normalizedTitleKey(canonicalTitle)) return false;
    const auto candidate = findAnimeByTitle(alias);
    if (!candidate || normalizedTitleKey(candidate->displayTitle) != key) return false;
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    // RSS may suggest a title only for an untouched local entry. A user alias or
    // Bangumi binding is authoritative and must never be replaced by feed text.
    auto update = prepare(db,
        "UPDATE anime SET display_title=?,updated_at=CURRENT_TIMESTAMP WHERE id=? "
        "AND display_title=? AND locked=0 AND bangumi_subject_id IS NULL "
        "AND NOT EXISTS(SELECT 1 FROM anime_alias WHERE anime_id=? AND source='user')");
    bind(update.get(), 1, canonicalTitle);
    sqlite3_bind_int64(update.get(), 2, candidate->id);
    bind(update.get(), 3, candidate->displayTitle);
    sqlite3_bind_int64(update.get(), 4, candidate->id);
    done(db, update.get());
    if (sqlite3_changes(db) == 0) return false;
    auto insert = prepare(db,
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(?,?,'mikan') "
        "ON CONFLICT(anime_id,normalized_alias) DO NOTHING");
    sqlite3_bind_int64(insert.get(), 1, candidate->id);
    bind(insert.get(), 2, key);
    done(db, insert.get());
    auto media = prepare(db,
        "UPDATE media_file SET title=?,updated_at=CURRENT_TIMESTAMP WHERE anime_id=? AND title=?");
    bind(media.get(), 1, canonicalTitle);
    sqlite3_bind_int64(media.get(), 2, candidate->id);
    bind(media.get(), 3, candidate->displayTitle);
    done(db, media.get());
    auto entry = prepare(db,
        "INSERT INTO audit_log(action,entity_type,entity_id,details_json) "
        "VALUES('mikan_title_adopted','anime',?,'{}')");
    bind(entry.get(), 1, std::to_string(candidate->id));
    done(db, entry.get());
    tx.commit();
    titleIndexChanges_ = -1;
    titleIndexDataVersion_ = -1;
    titleIndex_.clear();
    return true;
}

std::optional<AnimeRecord> SqliteMediaRepository::findAnimeByTitle(
    const std::string& title, std::optional<std::string> season) const {
    if (title.empty() || title.size() > 500) return std::nullopt;
    const auto key = normalizedTitleKey(title);
    if (key.empty()) return std::nullopt;
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    const auto changes = sqlite3_total_changes64(db);
    const auto currentDataVersion = dataVersion(db);
    if (titleIndexChanges_ != changes || titleIndexDataVersion_ != currentDataVersion) {
        std::unordered_map<std::string, std::vector<AnimeRecord>> refreshed;
        auto stmt = prepare(db,
            "SELECT a.id,a.display_title,a.original_title,a.season,a.year,a.bangumi_subject_id,a.cover_url,a.locked,"
            "a.display_title,a.original_title FROM anime a "
            "UNION ALL "
            "SELECT a.id,a.display_title,a.original_title,a.season,a.year,a.bangumi_subject_id,a.cover_url,a.locked,"
            "x.normalized_alias,'' FROM anime_alias x JOIN anime a ON a.id=x.anime_id");
        int rc;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            const auto anime = animeRow(stmt.get());
            addTitleIndexEntry(refreshed, normalizedTitleKey(column(stmt.get(), 8)), anime);
            addTitleIndexEntry(refreshed, normalizedTitleKey(column(stmt.get(), 9)), anime);
        }
        if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        titleIndex_ = std::move(refreshed);
        titleIndexChanges_ = changes;
        titleIndexDataVersion_ = currentDataVersion;
    }
    std::optional<AnimeRecord> found;
    const auto matches = titleIndex_.find(key);
    if (matches == titleIndex_.end()) return std::nullopt;
    for (const auto& anime : matches->second) {
        if (season && anime.season != *season) continue;
        if (found) return std::nullopt;
        found = anime;
    }
    return found;
}
} // namespace anime_vault
