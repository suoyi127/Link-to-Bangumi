#pragma once

#include "SqliteSupport.hpp"
#include "anime_vault/domain/MediaRecords.hpp"
#include "anime_vault/domain/BangumiMetadata.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace anime_vault::sqlite_rows {
using namespace sqlite_support;

inline ScanRecord scanRow(sqlite3_stmt* stmt) {
    return {sqlite3_column_int64(stmt, 0), column(stmt, 1), column(stmt, 2),
        sqlite3_column_int64(stmt, 3), sqlite3_column_int64(stmt, 4),
        sqlite3_column_int64(stmt, 5), column(stmt, 6)};
}
inline PlanRecord planRow(sqlite3_stmt* stmt) {
    return {sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1),
        column(stmt, 2), sqlite3_column_int64(stmt, 3), column(stmt, 4),
        column(stmt, 5), column(stmt, 6), column(stmt, 7), column(stmt, 8), column(stmt, 9)};
}
inline MediaRecord mediaRow(sqlite3_stmt* stmt) {
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
    if (sqlite3_column_type(stmt, 17) != SQLITE_NULL) {
        record.folderImportId = sqlite3_column_int64(stmt, 17);
        record.origin = "folder_import";
    }
    record.rawEpisodeNumber = column(stmt, 18);
    record.episodeNumberSource = column(stmt, 19);
    return record;
}
inline OrganizationJobRecord jobRow(sqlite3_stmt* stmt) {
    return {sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1), column(stmt, 2),
            column(stmt, 3), column(stmt, 4), column(stmt, 5), sqlite3_column_int64(stmt, 6)};
}
inline std::optional<OrganizationJobRecord> findJobByKey(sqlite3* db, const std::string& key) {
    auto stmt = prepare(db, "SELECT id,plan_id,idempotency_key,status,result_path,failure_details,result_bytes FROM organize_job WHERE idempotency_key=?");
    sqlite_support::bind(stmt.get(), 1, key);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return jobRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}
inline void audit(sqlite3* db, std::int64_t jobId, const char* action, const std::string& code) {
    auto stmt = prepare(db, "INSERT INTO audit_log(action,entity_type,entity_id,details_json) VALUES(?,'organize_job',?,?)");
    sqlite_support::bind(stmt.get(), 1, action);
    sqlite_support::bind(stmt.get(), 2, std::to_string(jobId));
    sqlite_support::bind(stmt.get(), 3, "{\"code\":\"" + code + "\"}");
    done(db, stmt.get());
}
inline AnimeRecord animeRow(sqlite3_stmt* stmt) {
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
    if (sqlite3_column_count(stmt) > 8 && std::string_view(sqlite3_column_name(stmt, 8)) == "imported_month")
        record.importedMonth = column(stmt, 8);
    return record;
}
inline void storeEpisodeMappings(sqlite3* db, const BangumiSubject& subject) {
    // 仅接收一一对应的正整数正片映射；异常映射整批忽略。
    bool validMapping = !subject.episodes.empty() && subject.episodes.size() <= 1000;
    std::unordered_set<std::string> sorts, eps;
    const auto validNumber = [](const std::string& value) {
        if (value.empty() || value.size() > 5 || value.front() == '0' ||
            !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) return false;
        return std::stoi(value) <= 10000;
    };
    for (const auto& mapping : subject.episodes) {
        if (!validNumber(mapping.sort) || !validNumber(mapping.ep) ||
            !sorts.insert(mapping.sort).second || !eps.insert(mapping.ep).second) validMapping = false;
    }
    if (validMapping) {
        auto remove = prepare(db, "DELETE FROM bangumi_episode_mapping WHERE subject_id=?");
        sqlite3_bind_int64(remove.get(), 1, subject.id); done(db, remove.get());
        for (const auto& mapping : subject.episodes) {
            auto insert = prepare(db, "INSERT INTO bangumi_episode_mapping(subject_id,sort_number,ep_number) VALUES(?,?,?)");
            sqlite3_bind_int64(insert.get(), 1, subject.id);
            sqlite_support::bind(insert.get(), 2, mapping.sort); sqlite_support::bind(insert.get(), 3, mapping.ep); done(db, insert.get());
        }
    }
}

inline void mapAnimeEpisodes(sqlite3* db, std::int64_t animeId) {
    auto update = prepare(db,
        "UPDATE media_file SET episode_number=COALESCE((SELECT e.ep_number FROM bangumi_episode_mapping e JOIN anime a ON a.bangumi_subject_id=e.subject_id WHERE a.id=media_file.anime_id AND e.sort_number=media_file.raw_episode_number),raw_episode_number),"
        "episode_number_source=CASE WHEN EXISTS(SELECT 1 FROM bangumi_episode_mapping e JOIN anime a ON a.bangumi_subject_id=e.subject_id WHERE a.id=media_file.anime_id AND e.sort_number=media_file.raw_episode_number) THEN 'bangumi' ELSE 'parsed' END,updated_at=CURRENT_TIMESTAMP "
        "WHERE anime_id=? AND episode_number_source!='manual' AND episode_type='normal'");
    sqlite3_bind_int64(update.get(), 1, animeId);
    done(db, update.get());
}

inline void fillAnime(sqlite3* db, AnimeRecord& record, std::int64_t mediaOffset, int mediaLimit) {
    auto aliases = prepare(db, "SELECT normalized_alias FROM anime_alias WHERE anime_id=? ORDER BY normalized_alias LIMIT 100");
    sqlite3_bind_int64(aliases.get(), 1, record.id);
    int rc;
    while ((rc = sqlite3_step(aliases.get())) == SQLITE_ROW)
        record.aliases.push_back(column(aliases.get(), 0));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    auto media = prepare(db, "SELECT m.id,m.scan_id,m.source_path,m.filename,m.episode_number,m.episode_type,m.size_bytes,m.status,m.confidence,m.title,m.season,a.bangumi_subject_id,m.source_modified_at,m.origin,m.anime_id,m.parsed_title,m.library_path,m.folder_import_id,m.raw_episode_number,m.episode_number_source FROM media_file m JOIN anime a ON a.id=m.anime_id WHERE a.id=? AND m.status!='missing' ORDER BY m.id LIMIT ? OFFSET ?");
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

} // namespace anime_vault::sqlite_rows
