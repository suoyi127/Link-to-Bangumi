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

std::vector<AnimeRecord> SqliteMediaRepository::listAnime() const {
    return listAnimePage(0, 200).items;
}

AnimePage SqliteMediaRepository::listAnimePage(std::int64_t offset, int limit, const std::string& sort) const {
    if (sort != "import_desc" && sort != "import_asc") throw std::invalid_argument("invalid anime sort");
    if (offset < 0 || offset > 1'000'000 || limit < 1 || limit > 200)
        throw std::invalid_argument("invalid anime page");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    const auto sql = std::string("SELECT a.id,a.display_title,a.original_title,a.season,a.year,a.bangumi_subject_id,a.cover_url,a.locked,strftime('%Y-%m',a.created_at) AS imported_month FROM anime a WHERE NOT EXISTS (SELECT 1 FROM media_file m WHERE m.anime_id=a.id) OR EXISTS (SELECT 1 FROM media_file m WHERE m.anime_id=a.id AND m.status!='missing') ORDER BY strftime('%Y-%m',a.created_at) ") + (sort == "import_asc" ? "ASC" : "DESC") + ",a.id DESC LIMIT ? OFFSET ?";
    auto stmt = prepare(db, sql.c_str());
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

std::vector<AnimeRecord> SqliteMediaRepository::listAnimeForScan(std::int64_t scanId) const {
    if (scanId <= 0) throw std::invalid_argument("invalid scan id");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT a.id,a.display_title,a.original_title,a.season,a.year,"
        "a.bangumi_subject_id,a.cover_url,a.locked FROM anime a WHERE EXISTS "
        "(SELECT 1 FROM media_file m WHERE m.anime_id=a.id AND m.scan_id=? AND m.status!='missing') "
        "ORDER BY a.id DESC");
    sqlite3_bind_int64(stmt.get(), 1, scanId);
    std::vector<AnimeRecord> items;
    int rc;
    while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) items.push_back(animeRow(stmt.get()));
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    return items;
}

std::optional<AnimeRecord> SqliteMediaRepository::findAnimeByBangumiSubject(std::int64_t subjectId) const {
    if (subjectId <= 0) return std::nullopt;
    std::lock_guard lock(database_.mutex());
    auto stmt = prepare(database_.handle(), "SELECT id,display_title,original_title,season,year,bangumi_subject_id,cover_url,locked FROM anime WHERE bangumi_subject_id=?");
    sqlite3_bind_int64(stmt.get(), 1, subjectId);
    const auto rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return animeRow(stmt.get());
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_.handle()));
    return std::nullopt;
}

std::optional<AnimeRecord> SqliteMediaRepository::getAnime(std::int64_t id,
                                                           std::int64_t mediaOffset,
                                                           int mediaLimit) const {
    if (mediaOffset < 0 || mediaOffset > 1'000'000 || mediaLimit < 1 || mediaLimit > 200)
        throw std::invalid_argument("invalid anime media page");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,display_title,original_title,season,year,bangumi_subject_id,cover_url,locked,strftime('%Y-%m',created_at) AS imported_month FROM anime WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    if (rc != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
    auto record = animeRow(stmt.get());
    fillAnime(db, record, mediaOffset, mediaLimit);
    return record;
}

AnimeRecord SqliteMediaRepository::editAnime(std::int64_t id, const AnimeEdit& edit) {
    if (id <= 0) throw AnimeBindingError("invalid_id");
    const auto hasControl = [](const std::string& value) {
        return std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
    };
    if (edit.displayTitle.empty() || edit.displayTitle.size() > 200 || edit.season.size() > 40 ||
        normalizedTitleKey(edit.displayTitle).empty() || edit.displayTitle.back() == '.' || edit.displayTitle.back() == ' ' ||
        edit.coverUrl.size() > 2048 || edit.displayTitle.find_first_of("/\\:*?\"<>|") != std::string::npos ||
        hasControl(edit.displayTitle) || hasControl(edit.season) || hasControl(edit.coverUrl) ||
        (!edit.coverUrl.empty() && !edit.coverUrl.starts_with("https://") && !edit.coverUrl.starts_with("http://") && !edit.coverUrl.starts_with("/api/covers/")))
        throw AnimeBindingError("invalid_anime_edit");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    const auto old = getAnime(id);
    if (!old) throw AnimeBindingError("anime_not_found");
    auto stmt = prepare(db, "UPDATE anime SET display_title=?,season=?,cover_url=?,updated_at=CURRENT_TIMESTAMP WHERE id=?");
    sqlite_support::bind(stmt.get(), 1, edit.displayTitle); sqlite_support::bind(stmt.get(), 2, edit.season); sqlite_support::bind(stmt.get(), 3, edit.coverUrl);
    sqlite3_bind_int64(stmt.get(), 4, id); done(db, stmt.get());
    // 保存人工别名，后续扫描仍能将原文件名识别为同一部番剧。
    for (const auto& title : {old->displayTitle, edit.displayTitle}) {
        auto alias = prepare(db, "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(?,?,'user') ON CONFLICT(anime_id,normalized_alias) DO UPDATE SET source='user'");
        sqlite3_bind_int64(alias.get(), 1, id); sqlite_support::bind(alias.get(), 2, normalizedTitleKey(title)); done(db, alias.get());
    }
    auto media = prepare(db, "UPDATE media_file SET title=?,season=?,updated_at=CURRENT_TIMESTAMP WHERE anime_id=?");
    sqlite_support::bind(media.get(), 1, edit.displayTitle); sqlite_support::bind(media.get(), 2, edit.season); sqlite3_bind_int64(media.get(), 3, id); done(db, media.get());
    auto audit = prepare(db, "INSERT INTO audit_log(action,entity_type,entity_id,details_json) VALUES('anime_edited','anime',?,'{}')");
    sqlite_support::bind(audit.get(), 1, std::to_string(id)); done(db, audit.get());
    tx.commit(); return *getAnime(id);
}

AnimeRecord SqliteMediaRepository::bindAnime(std::int64_t id, const BangumiSubject& subject,
                                             bool canonicalizeTitle) {
    if (id <= 0) throw AnimeBindingError("invalid_id");
    if (subject.id <= 0 || subject.type != 2 || subject.name.empty() ||
        subject.name.size() > 500 || subject.nameCn.size() > 500 ||
        subject.coverUrl.size() > 2048 || subject.date.size() > 32)
        throw AnimeBindingError("invalid_subject");
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto current = prepare(db, "SELECT bangumi_subject_id,locked,display_title FROM anime WHERE id=?");
    sqlite3_bind_int64(current.get(), 1, id);
    if (sqlite3_step(current.get()) != SQLITE_ROW) throw AnimeBindingError("anime_not_found");
    const bool hadBinding = sqlite3_column_type(current.get(), 0) != SQLITE_NULL;
    const bool same = hadBinding &&
        sqlite3_column_int64(current.get(), 0) == subject.id;
    const bool locked = sqlite3_column_int(current.get(), 1) != 0;
    const auto* oldTitleRaw = reinterpret_cast<const char*>(sqlite3_column_text(current.get(), 2));
    const std::string oldTitle = oldTitleRaw ? oldTitleRaw : "";
    if (locked && !same) throw AnimeBindingError("anime_locked");
    auto owner = prepare(db, "SELECT id,display_title FROM anime WHERE bangumi_subject_id=? AND id<>?");
    sqlite3_bind_int64(owner.get(), 1, subject.id);
    sqlite3_bind_int64(owner.get(), 2, id);
    if (sqlite3_step(owner.get()) == SQLITE_ROW) throw AnimeBindingError("bangumi_subject_in_use", sqlite3_column_int64(owner.get(), 0), reinterpret_cast<const char*>(sqlite3_column_text(owner.get(), 1)));
    if (!same || !sqlite3_column_int(current.get(), 1)) {
        const bool rebinding = hadBinding && !same;
        auto update = prepare(db, rebinding
            ? "UPDATE anime SET bangumi_subject_id=?,original_title=?,cover_url=?,year=?,updated_at=CURRENT_TIMESTAMP WHERE id=?"
            : "UPDATE anime SET bangumi_subject_id=?,original_title=?,cover_url=CASE WHEN cover_url IS NULL OR cover_url='' THEN ? ELSE cover_url END,year=COALESCE(year,?),updated_at=CURRENT_TIMESTAMP WHERE id=?");
        sqlite3_bind_int64(update.get(), 1, subject.id);
        sqlite_support::bind(update.get(), 2, subject.name);
        sqlite_support::bind(update.get(), 3, subject.coverUrl);
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
            sqlite_support::bind(insert.get(), 2, alias);
            done(db, insert.get());
        }
        for (const auto& name : subject.aliases) {
            const auto alias = normalizedTitleKey(name);
            if (alias.empty()) continue;
            auto insert = prepare(db, "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(?,?,'bangumi') ON CONFLICT(anime_id,normalized_alias) DO NOTHING");
            sqlite3_bind_int64(insert.get(), 1, id);
            sqlite_support::bind(insert.get(), 2, alias);
            done(db, insert.get());
        }
        if (canonicalizeTitle && !hadBinding && !locked && !subject.nameCn.empty() &&
            normalizedTitleKey(oldTitle) != normalizedTitleKey(subject.nameCn)) {
            auto userAlias = prepare(db, "SELECT 1 FROM anime_alias WHERE anime_id=? AND source IN ('user','mikan') LIMIT 1");
            sqlite3_bind_int64(userAlias.get(), 1, id);
            if (sqlite3_step(userAlias.get()) != SQLITE_ROW) {
                // 只改写未经用户整理的展示标题，原始罗马音保留为 Bangumi 别名。
                auto rename = prepare(db, "UPDATE anime SET display_title=? WHERE id=? AND display_title=?");
                sqlite_support::bind(rename.get(), 1, subject.nameCn);
                sqlite3_bind_int64(rename.get(), 2, id);
                sqlite_support::bind(rename.get(), 3, oldTitle);
                done(db, rename.get());
                const auto alias = normalizedTitleKey(oldTitle);
                if (!alias.empty()) {
                    auto insert = prepare(db, "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(?,?,'bangumi') ON CONFLICT(anime_id,normalized_alias) DO NOTHING");
                    sqlite3_bind_int64(insert.get(), 1, id);
                    sqlite_support::bind(insert.get(), 2, alias);
                    done(db, insert.get());
                }
                auto media = prepare(db, "UPDATE media_file SET title=? WHERE anime_id=? AND title=?");
                sqlite_support::bind(media.get(), 1, subject.nameCn);
                sqlite3_bind_int64(media.get(), 2, id);
                sqlite_support::bind(media.get(), 3, oldTitle);
                done(db, media.get());
            }
        }
        auto auditEntry = prepare(db, "INSERT INTO audit_log(action,entity_type,entity_id,details_json) VALUES('anime_bound','anime',?,'{}')");
        sqlite_support::bind(auditEntry.get(), 1, std::to_string(id));
        done(db, auditEntry.get());
    }
    storeEpisodeMappings(db, subject);
    mapAnimeEpisodes(db, id);
    tx.commit();
    return *getAnime(id);
}

void SqliteMediaRepository::applyAnimeEpisodeMapping(std::int64_t id) {
    mapAnimeEpisodes(database_.handle(), id);
}

std::optional<AnimeRecord> SqliteMediaRepository::nextAnimeNeedingEpisodeMapping(std::int64_t afterId) const {
    std::lock_guard lock(database_.mutex());
    auto stmt = prepare(database_.handle(), "SELECT a.id,a.display_title,a.original_title,a.season,a.year,a.bangumi_subject_id,a.cover_url,a.locked FROM anime a "
        "WHERE a.id>? AND a.bangumi_subject_id IS NOT NULL AND EXISTS(SELECT 1 FROM media_file m WHERE m.anime_id=a.id AND m.status!='missing' AND m.episode_type='normal' AND m.episode_number_source='parsed' AND m.raw_episode_number!='') ORDER BY a.id LIMIT 1");
    sqlite3_bind_int64(stmt.get(), 1, afterId);
    const auto rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return animeRow(stmt.get());
    if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_.handle()));
    return std::nullopt;
}

bool SqliteMediaRepository::updateEpisodeMappingsIfBound(std::int64_t id, const BangumiSubject& subject) {
    if (subject.id <= 0 || subject.type != 2 || subject.episodes.empty()) return false;
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto current = prepare(db, "SELECT 1 FROM anime WHERE id=? AND bangumi_subject_id=?");
    sqlite3_bind_int64(current.get(), 1, id); sqlite3_bind_int64(current.get(), 2, subject.id);
    if (sqlite3_step(current.get()) != SQLITE_ROW) return false;
    // 异步回包只能更新仍绑定同一词条的文件，不改写标题或人工集数。
    storeEpisodeMappings(db, subject);
    mapAnimeEpisodes(db, id);
    tx.commit();
    return true;
}

bool SqliteMediaRepository::updateCoverIfBound(std::int64_t animeId, std::int64_t subjectId,
                                                const std::string& localUrl, std::optional<std::string> expectedCover) {
    if (animeId <= 0 || subjectId <= 0 || localUrl.empty() || localUrl.size() > 2048 ||
        !localUrl.starts_with("/api/covers/")) return false;
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto update = prepare(db,
        "UPDATE anime SET cover_url=?,updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND bangumi_subject_id=? AND (? IS NULL OR COALESCE(cover_url,'')=?)");
    sqlite_support::bind(update.get(), 1, localUrl);
    sqlite3_bind_int64(update.get(), 2, animeId);
    sqlite3_bind_int64(update.get(), 3, subjectId);
    if (expectedCover) { sqlite_support::bind(update.get(), 4, *expectedCover); sqlite_support::bind(update.get(), 5, *expectedCover); }
    else { sqlite3_bind_null(update.get(), 4); sqlite3_bind_null(update.get(), 5); }
    done(db, update.get());
    const bool updated = sqlite3_changes(db) == 1;
    if (updated) {
        auto audit = prepare(db,
            "INSERT INTO audit_log(action,entity_type,entity_id,details_json) "
            "VALUES('cover_cached','anime',?,'{}')");
        sqlite_support::bind(audit.get(), 1, std::to_string(animeId));
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
    auto candidate = findAnimeByTitle(alias);
    const bool aliasFound = candidate.has_value();
    const auto canonicalKey = normalizedTitleKey(canonicalTitle);
    if (!candidate) candidate = findAnimeByTitle(canonicalTitle);
    if (!candidate) return false;
    const auto displayKey = normalizedTitleKey(candidate->displayTitle);
    if (displayKey != key && displayKey != canonicalKey) return false;
    std::lock_guard lock(database_.mutex());
    // 别名有多个归属时不能退回规范标题猜测；已保存的映射无需重复改写。
    if (!aliasFound && titleIndex_.contains(key)) return false;
    auto* db = database_.handle();
    Transaction tx(db);
    auto existing = prepare(db, "SELECT 1 FROM anime_alias WHERE anime_id=? AND normalized_alias=?");
    sqlite3_bind_int64(existing.get(), 1, candidate->id);
    sqlite_support::bind(existing.get(), 2, key);
    if (sqlite3_step(existing.get()) == SQLITE_ROW) return false;
    // RSS may suggest a title only for an untouched local entry. A user alias or
    // Bangumi binding is authoritative and must never be replaced by feed text.
    auto update = prepare(db,
        "UPDATE anime SET display_title=?,updated_at=CURRENT_TIMESTAMP WHERE id=? "
        "AND display_title=? AND locked=0 AND bangumi_subject_id IS NULL "
        "AND NOT EXISTS(SELECT 1 FROM anime_alias WHERE anime_id=? AND source='user')");
    sqlite_support::bind(update.get(), 1, canonicalTitle);
    sqlite3_bind_int64(update.get(), 2, candidate->id);
    sqlite_support::bind(update.get(), 3, candidate->displayTitle);
    sqlite3_bind_int64(update.get(), 4, candidate->id);
    done(db, update.get());
    if (sqlite3_changes(db) == 0) return false;
    auto insert = prepare(db,
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(?,?,'mikan') "
        "ON CONFLICT(anime_id,normalized_alias) DO NOTHING");
    sqlite3_bind_int64(insert.get(), 1, candidate->id);
    sqlite_support::bind(insert.get(), 2, key);
    done(db, insert.get());
    auto media = prepare(db,
        "UPDATE media_file SET title=?,updated_at=CURRENT_TIMESTAMP WHERE anime_id=? AND title=?");
    sqlite_support::bind(media.get(), 1, canonicalTitle);
    sqlite3_bind_int64(media.get(), 2, candidate->id);
    sqlite_support::bind(media.get(), 3, candidate->displayTitle);
    done(db, media.get());
    auto entry = prepare(db,
        "INSERT INTO audit_log(action,entity_type,entity_id,details_json) "
        "VALUES('mikan_title_adopted','anime',?,'{}')");
    sqlite_support::bind(entry.get(), 1, std::to_string(candidate->id));
    done(db, entry.get());
    tx.commit();
    titleIndexChanges_ = -1;
    titleIndexDataVersion_ = -1;
    titleIndex_.clear();
    return true;
}

} // namespace anime_vault
