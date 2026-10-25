#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <regex>

#include "SqliteSupport.hpp"
#include "SqliteRepositoryRows.hpp"

namespace anime_vault {
using namespace sqlite_support;
using namespace sqlite_rows;

std::int64_t SqliteMediaRepository::createScan(const ScanRecord& record) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "INSERT INTO scan_job(source,status,discovered_count,processed_count,error_count,error_summary) VALUES(?,?,?,?,?,?)");
    sqlite_support::bind(stmt.get(), 1, record.source); sqlite_support::bind(stmt.get(), 2, record.status);
    sqlite3_bind_int64(stmt.get(), 3, record.discoveredCount);
    sqlite3_bind_int64(stmt.get(), 4, record.processedCount);
    sqlite3_bind_int64(stmt.get(), 5, record.errorCount);
    sqlite_support::bind(stmt.get(), 6, record.errorSummary);
    done(db, stmt.get());
    return sqlite3_last_insert_rowid(db);
}

FolderImportRecord SqliteMediaRepository::addFolderImport(const std::string& rootPath) {
    if (rootPath.empty() || rootPath.size() > 2048) throw std::invalid_argument("invalid folder path");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto insert = prepare(db, "INSERT INTO folder_import(root_path) VALUES(?) ON CONFLICT(root_path) DO NOTHING");
    sqlite_support::bind(insert.get(), 1, rootPath);
    done(db, insert.get());
    auto lookup = prepare(db, "SELECT id,root_path FROM folder_import WHERE root_path=?");
    sqlite_support::bind(lookup.get(), 1, rootPath);
    if (sqlite3_step(lookup.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    return {sqlite3_column_int64(lookup.get(), 0), column(lookup.get(), 1)};
}

std::vector<FolderImportRecord> SqliteMediaRepository::listFolderImports() const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,root_path FROM folder_import ORDER BY id");
    std::vector<FolderImportRecord> folders;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW)
        folders.push_back({sqlite3_column_int64(stmt.get(), 0), column(stmt.get(), 1)});
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    return folders;
}

std::optional<FolderImportRecord> SqliteMediaRepository::getFolderImport(std::int64_t id) const {
    if (id <= 0) return std::nullopt;
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,root_path FROM folder_import WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const auto rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return FolderImportRecord{sqlite3_column_int64(stmt.get(), 0), column(stmt.get(), 1)};
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
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
    sqlite_support::bind(stmt.get(), 1, record.status);
    sqlite3_bind_int64(stmt.get(), 2, record.discoveredCount);
    sqlite3_bind_int64(stmt.get(), 3, record.processedCount);
    sqlite3_bind_int64(stmt.get(), 4, record.errorCount);
    sqlite_support::bind(stmt.get(), 5, record.errorSummary);
    sqlite3_bind_int64(stmt.get(), 6, record.id);
    done(db, stmt.get());
    if (sqlite3_changes(db) == 0) throw std::runtime_error("scan not found");
}

std::int64_t SqliteMediaRepository::insertMedia(const MediaRecord& record) {
    std::lock_guard lock(database_.mutex());
    if (record.origin != "qb_download" && record.origin != "external_import" &&
        record.origin != "folder_import")
        throw std::invalid_argument("invalid media origin");
    if ((record.origin == "folder_import") != record.folderImportId.has_value() ||
        (record.folderImportId && *record.folderImportId <= 0))
        throw std::invalid_argument("invalid folder import ID");
    auto* db = database_.handle();
    const auto beforeChanges = sqlite3_total_changes64(db);
    auto stmt = prepare(db, "INSERT INTO media_file(scan_id,source_path,filename,episode_number,episode_type,size_bytes,status,confidence,source_modified_at,origin,parsed_title,folder_import_id,raw_episode_number) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(source_path) DO UPDATE SET scan_id=excluded.scan_id,filename=excluded.filename,size_bytes=excluded.size_bytes,source_modified_at=excluded.source_modified_at,status=CASE WHEN media_file.status='missing' AND media_file.library_path IS NULL THEN 'inbox' ELSE media_file.status END,updated_at=CURRENT_TIMESTAMP WHERE media_file.origin=excluded.origin AND media_file.folder_import_id IS excluded.folder_import_id");
    sqlite3_bind_int64(stmt.get(), 1, record.scanId);
    sqlite_support::bind(stmt.get(), 2, record.sourcePath); sqlite_support::bind(stmt.get(), 3, record.filename);
    sqlite_support::bind(stmt.get(), 4, record.episodeNumber); sqlite_support::bind(stmt.get(), 5, record.episodeType);
    sqlite3_bind_int64(stmt.get(), 6, record.sizeBytes);
    sqlite_support::bind(stmt.get(), 7, record.status);
    sqlite3_bind_double(stmt.get(), 8, record.confidence);
    sqlite_support::bind(stmt.get(), 9, record.sourceModifiedAt);
    sqlite_support::bind(stmt.get(), 10, record.origin == "folder_import" ? "external_import" : record.origin);
    sqlite_support::bind(stmt.get(), 11, record.parsedTitle);
    if (record.folderImportId) sqlite3_bind_int64(stmt.get(), 12, *record.folderImportId);
    else sqlite3_bind_null(stmt.get(), 12);
    sqlite_support::bind(stmt.get(), 13, record.episodeNumber);
    done(db, stmt.get());
    if (sqlite3_changes(db) == 0)
        throw std::invalid_argument("media origin conflicts with existing source path");
    auto lookup = prepare(db, "SELECT id FROM media_file WHERE source_path=?");
    sqlite_support::bind(lookup.get(), 1, record.sourcePath);
    if (sqlite3_step(lookup.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    const auto id = sqlite3_column_int64(lookup.get(), 0);
    if (titleIndexChanges_ == beforeChanges)
        titleIndexChanges_ = sqlite3_total_changes64(db);
    return id;
}

void SqliteMediaRepository::markMissingMedia(const std::string& origin,
                                              const std::vector<std::string>& observedPaths,
                                              std::optional<std::int64_t> folderImportId) {
    if (origin != "qb_download" && origin != "external_import" && origin != "folder_import")
        throw std::invalid_argument("invalid media origin");
    if ((origin == "folder_import") != folderImportId.has_value())
        throw std::invalid_argument("invalid folder import ID");
    const std::unordered_set<std::string> observed(observedPaths.begin(), observedPaths.end());
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto candidates = prepare(db, "SELECT id,source_path FROM media_file "
        "WHERE origin=? AND folder_import_id IS ? AND status='inbox' AND library_path IS NULL");
    sqlite_support::bind(candidates.get(), 1, origin == "folder_import" ? "external_import" : origin);
    if (folderImportId) sqlite3_bind_int64(candidates.get(), 2, *folderImportId);
    else sqlite3_bind_null(candidates.get(), 2);
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
    auto stmt = prepare(db, "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path,m.folder_import_id,m.raw_episode_number,m.episode_number_source FROM media_file m LEFT JOIN anime a ON a.id=m.anime_id WHERE m.id=?");
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
    auto stmt = prepare(db, "UPDATE media_file SET episode_number=?,raw_episode_number=?,episode_number_source='parsed',episode_type=?,confidence=?,updated_at=CURRENT_TIMESTAMP WHERE id=? AND episode_number_source!='manual'");
    sqlite_support::bind(stmt.get(), 1, episodeNumber); sqlite_support::bind(stmt.get(), 2, episodeNumber); sqlite_support::bind(stmt.get(), 3, episodeType);
    sqlite3_bind_double(stmt.get(), 4, confidence);
    sqlite3_bind_int64(stmt.get(), 5, id);
    done(db, stmt.get());
    if (sqlite3_changes(db) == 0) throw std::runtime_error("media not found");
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

std::vector<MediaRecord> SqliteMediaRepository::listInbox() const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path,m.folder_import_id,m.raw_episode_number,m.episode_number_source FROM media_file m LEFT JOIN anime a ON a.id=m.anime_id WHERE m.status='inbox' AND a.bangumi_subject_id IS NULL ORDER BY m.id");
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
    if (origin && *origin != "qb_download" && *origin != "external_import" && *origin != "folder_import")
        throw std::invalid_argument("invalid inbox origin");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    const bool filtered = origin.has_value();
    auto count = prepare(db, filtered
        ? "SELECT COUNT(*) FROM media_file WHERE status='inbox' AND NOT EXISTS (SELECT 1 FROM anime a WHERE a.id=media_file.anime_id AND a.bangumi_subject_id IS NOT NULL) AND (CASE WHEN folder_import_id IS NOT NULL THEN 'folder_import' ELSE origin END)=?"
        : "SELECT COUNT(*) FROM media_file WHERE status='inbox' AND NOT EXISTS (SELECT 1 FROM anime a WHERE a.id=media_file.anime_id AND a.bangumi_subject_id IS NOT NULL)");
    if (filtered) sqlite_support::bind(count.get(), 1, *origin);
    if (sqlite3_step(count.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    InboxPage page;
    page.total = sqlite3_column_int64(count.get(), 0);
    auto stmt = prepare(db, filtered
        ? "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path,m.folder_import_id,m.raw_episode_number,m.episode_number_source FROM media_file m LEFT JOIN anime a ON a.id=m.anime_id WHERE m.status='inbox' AND a.bangumi_subject_id IS NULL AND (CASE WHEN m.folder_import_id IS NOT NULL THEN 'folder_import' ELSE m.origin END)=? ORDER BY m.id LIMIT ? OFFSET ?"
        : "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path,m.folder_import_id,m.raw_episode_number,m.episode_number_source FROM media_file m LEFT JOIN anime a ON a.id=m.anime_id WHERE m.status='inbox' AND a.bangumi_subject_id IS NULL ORDER BY m.id LIMIT ? OFFSET ?");
    // Count and page use the same filter so a busy source cannot hide the other source.
    if (filtered) sqlite_support::bind(stmt.get(), 1, *origin);
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

void SqliteMediaRepository::updateMediaEpisode(std::int64_t id, const std::string& number, const std::string& type) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto stmt = prepare(db, "UPDATE media_file SET episode_number=?,episode_type=?,episode_number_source='manual',confidence=1,updated_at=CURRENT_TIMESTAMP WHERE id=?");
    sqlite_support::bind(stmt.get(), 1, number); sqlite_support::bind(stmt.get(), 2, type); sqlite3_bind_int64(stmt.get(), 3, id); done(db, stmt.get());
    auto audit = prepare(db, "INSERT INTO audit_log(action,entity_type,entity_id,details_json) VALUES('episode_edited','media_file',?,'{}')");
    sqlite_support::bind(audit.get(), 1, std::to_string(id)); done(db, audit.get()); tx.commit();
}

std::optional<MediaRecord> SqliteMediaRepository::findMediaBySourcePath(const std::string& path) const {
    std::lock_guard lock(database_.mutex());
    auto stmt = prepare(database_.handle(), "SELECT id FROM media_file WHERE source_path=?");
    sqlite_support::bind(stmt.get(), 1, path);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) return getMedia(sqlite3_column_int64(stmt.get(), 0));
    return std::nullopt;
}

} // namespace anime_vault
