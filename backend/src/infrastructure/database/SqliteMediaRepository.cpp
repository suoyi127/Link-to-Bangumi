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

namespace {
int dataVersion(sqlite3* db) {
    auto stmt = prepare(db, "PRAGMA data_version");
    if (sqlite3_step(stmt.get()) != SQLITE_ROW)
        throw std::runtime_error(sqlite3_errmsg(db));
    return sqlite3_column_int(stmt.get(), 0);
}
void addTitleIndexEntry(std::unordered_map<std::string, std::vector<AnimeRecord>>& index,
                        const std::string& key, const AnimeRecord& anime) {
    if (key.empty()) return;
    auto& records = index[key];
    if (std::none_of(records.begin(), records.end(), [&anime](const auto& item) {
            return item.id == anime.id;
        })) records.push_back(anime);
}
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
        auto anime = prepare(db, "INSERT INTO anime(id,display_title,season,bangumi_subject_id) VALUES((SELECT high_water+1 FROM anime_identity WHERE singleton=1),?,?,?) ON CONFLICT(bangumi_subject_id) DO NOTHING");
        sqlite_support::bind(anime.get(), 1, correction.title);
        sqlite_support::bind(anime.get(), 2, correction.season);
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
            sqlite_support::bind(current.get(), 2, correction.title);
            sqlite_support::bind(current.get(), 3, correction.season);
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
        if (!animeId && intent == MediaCorrectionIntent::automaticScan) {
            // 旧版本未提取 Season 字段；完整标题明确同季时复用唯一的既有绑定。
            static const std::regex explicitSeason(R"(\bSeason +([0-9]{1,2})$)", std::regex::icase);
            std::smatch season;
            if (std::regex_search(correction.title, season, explicitSeason) &&
                std::to_string(std::stoi(season[1].str())) == correction.season) {
                const auto legacy = findAnimeByTitle(correction.title);
                if (legacy && legacy->season.empty() && legacy->bangumiSubjectId) animeId = legacy->id;
            }
        }
        if (!animeId) {
            auto anime = prepare(db, "INSERT INTO anime(id,display_title,season) VALUES((SELECT high_water+1 FROM anime_identity WHERE singleton=1),?,?)");
            sqlite_support::bind(anime.get(), 1, correction.title);
            sqlite_support::bind(anime.get(), 2, correction.season);
            done(db, anime.get());
            animeId = sqlite3_last_insert_rowid(db);
        }
    }
    const bool preserveTitleIndex = intent == MediaCorrectionIntent::automaticScan &&
        sqlite3_total_changes64(db) == changesBeforeAnimeResolution &&
        titleIndexChanges_ == sqlite3_total_changes64(db) &&
        titleIndexDataVersion_ == dataVersion(db);
    auto resolvedTitle = correction.title;
    auto resolvedSeason = correction.season;
    if (intent == MediaCorrectionIntent::automaticScan) {
        auto manual = prepare(db, "SELECT display_title,season FROM anime WHERE id=? AND EXISTS (SELECT 1 FROM anime_alias WHERE anime_id=anime.id AND source='user')");
        sqlite3_bind_int64(manual.get(), 1, animeId);
        if (sqlite3_step(manual.get()) == SQLITE_ROW) { resolvedTitle = column(manual.get(), 0); resolvedSeason = column(manual.get(), 1); }
    }
    auto stmt = prepare(db, "UPDATE media_file SET title=?,season=?,episode_number=?,episode_type=?,confidence=?,anime_id=?,raw_episode_number=?,episode_number_source=?,updated_at=CURRENT_TIMESTAMP WHERE id=?");
    sqlite_support::bind(stmt.get(), 1, resolvedTitle);
    sqlite_support::bind(stmt.get(), 2, resolvedSeason);
    const bool manual = intent == MediaCorrectionIntent::userConfirmed || existingMedia->episodeNumberSource == "manual";
    sqlite_support::bind(stmt.get(), 3, intent == MediaCorrectionIntent::automaticScan && manual ? existingMedia->episodeNumber : correction.episodeNumber);
    sqlite_support::bind(stmt.get(), 4, intent == MediaCorrectionIntent::automaticScan && manual ? existingMedia->episodeType : correction.episodeType);
    sqlite3_bind_double(stmt.get(), 5, correction.confidence);
    sqlite3_bind_int64(stmt.get(), 6, animeId);
    sqlite_support::bind(stmt.get(), 7, existingMedia->rawEpisodeNumber);
    sqlite_support::bind(stmt.get(), 8, manual ? "manual" : "parsed");
    sqlite3_bind_int64(stmt.get(), 9, id);
    done(db, stmt.get());
    if (sqlite3_changes(db) == 0) throw std::runtime_error("media not found");
    if (!manual) mapAnimeEpisodes(db, animeId);
    if (intent == MediaCorrectionIntent::userConfirmed) {
        auto rename = prepare(db, "UPDATE anime SET display_title=?,updated_at=CURRENT_TIMESTAMP WHERE id=?");
        sqlite_support::bind(rename.get(), 1, correction.title);
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
            sqlite_support::bind(insert.get(), 2, alias);
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
    if (!found && season && matches->second.size() == 1) {
        // 人工纠正季数后仍接受唯一的人工别名；存在多个同名候选时保持严格季数匹配。
        const auto& candidate = matches->second.front();
        auto userAlias = prepare(db, "SELECT 1 FROM anime_alias WHERE anime_id=? AND normalized_alias=? AND source IN ('user','merged')");
        sqlite3_bind_int64(userAlias.get(), 1, candidate.id); sqlite_support::bind(userAlias.get(), 2, key);
        if (sqlite3_step(userAlias.get()) == SQLITE_ROW) return candidate;
    }
    return found;
}
} // namespace anime_vault
