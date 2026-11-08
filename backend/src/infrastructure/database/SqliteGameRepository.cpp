#include "anime_vault/infrastructure/database/SqliteGameRepository.hpp"
#include "SqliteRepositoryLock.hpp"
#include "SqliteLibrarySupport.hpp"
#include <algorithm>
#include <filesystem>
#include <set>
namespace anime_vault {
namespace {
using Statement = sqlite_support::Statement;
Statement prepare(sqlite3* db, const char* sql) { return sqlite_library_support::prepare<GameError>(db, sql, "game_storage_error"); }
std::string text(const Statement& statement, int column) { return sqlite_support::column(statement.get(), column); }
void bindText(const Statement& statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement.get(), index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) throw GameError("game_storage_error");
}
void done(const Statement& statement) { sqlite_library_support::done<GameError>(statement.get(), "game_storage_error"); }
namespace fs = std::filesystem;
fs::path filePath(const std::string& utf8) {
    return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}
bool sameFile(const fs::path& a, const fs::path& b) {
    std::error_code error;
    return fs::equivalent(a,b,error) && !error;
}
const GameResource* steamOwner(const fs::path& exe, const std::vector<GameResource>& games) {
    std::error_code error;
    if (!exe.is_absolute()) return nullptr;
    const auto file = fs::weakly_canonical(exe,error);
    if (error) return nullptr;
    const GameResource* owner = nullptr;
    for (const auto& game : games) {
        if (game.steamAppId.empty() || game.steamInstallPath.empty()) continue;
        const auto root = filePath(game.steamInstallPath);
        if (!root.is_absolute() || root == root.root_path()) continue;
        // 逐级比较真实目录，防止 Game Two 命中 Game，也不沿越界联接认领文件。
        for (auto parent=file.parent_path(); !parent.empty() && parent != parent.root_path(); parent=parent.parent_path()) {
            if (!sameFile(parent,root)) continue;
            if (owner) return nullptr;
            owner = &game;
            break;
        }
    }
    return owner;
}
bool bound(const GameResource& game) {
    return game.subjectId.has_value() || !game.vndbId.empty();
}
}
std::unique_ptr<RepositoryLock> SqliteGameRepository::lock() const { return std::make_unique<SqliteRepositoryLock>(db_); }
std::vector<GameResource> SqliteGameRepository::list() const {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "SELECT id,path,title,developer,summary,platform,subject_id,manual_metadata,cover IS NOT NULL,vndb_id,metadata_source,steam_app_id,steam_install_path,steam_manifest_path,steam_executable,steam_id,steam_stats_error,playtime_minutes,achievements_unlocked,achievements_total FROM game_resource WHERE id NOT IN (SELECT alias_id FROM game_import_alias) ORDER BY title,id");
    std::vector<GameResource> result; int state{};
    while ((state = sqlite3_step(statement.get())) == SQLITE_ROW) {
        GameResource game; game.id = sqlite3_column_int64(statement.get(), 0); game.path = text(statement, 1); game.title = text(statement, 2);
        game.developer = text(statement, 3); game.summary = text(statement, 4); game.platform = text(statement, 5);
        if (sqlite3_column_type(statement.get(), 6) != SQLITE_NULL) game.subjectId = sqlite3_column_int64(statement.get(), 6);
        game.manualMetadata = sqlite3_column_int(statement.get(), 7) != 0; game.hasCover = sqlite3_column_int(statement.get(), 8) != 0;
        game.vndbId = text(statement, 9); game.metadataSource = text(statement, 10);
        game.steamAppId = text(statement, 11); game.steamInstallPath = text(statement, 12);
        game.steamManifestPath = text(statement, 13); game.steamExecutable = text(statement, 14);
        game.steamId = text(statement, 15); game.steamStatsError = text(statement, 16);
        if (sqlite3_column_type(statement.get(), 17) != SQLITE_NULL) game.playtimeMinutes = sqlite3_column_int64(statement.get(), 17);
        if (sqlite3_column_type(statement.get(), 18) != SQLITE_NULL) game.achievementsUnlocked = sqlite3_column_int64(statement.get(), 18);
        if (sqlite3_column_type(statement.get(), 19) != SQLITE_NULL) game.achievementsTotal = sqlite3_column_int64(statement.get(), 19);
        result.push_back(std::move(game));
    }
    if (state != SQLITE_DONE) throw GameError("game_storage_error"); return result;
}
std::int64_t SqliteGameRepository::canonicalId(std::int64_t id) const {
    std::lock_guard lock(db_.mutex());
    const auto q=prepare(db_.handle(),"SELECT game_id FROM game_import_alias WHERE alias_id=?");
    sqlite3_bind_int64(q.get(),1,id);
    const auto state=sqlite3_step(q.get());
    if (state==SQLITE_ROW) return sqlite3_column_int64(q.get(),0);
    if (state!=SQLITE_DONE) throw GameError("game_storage_error");
    return id;
}
bool SqliteGameRepository::link(const GameResource& source, const GameResource& target) {
    if ((source.subjectId && target.subjectId && source.subjectId != target.subjectId) ||
        (!source.vndbId.empty() && !target.vndbId.empty() && source.vndbId != target.vndbId) ||
        (bound(source) && bound(target) && source.metadataSource != target.metadataSource) ||
        (source.manualMetadata && target.manualMetadata &&
        (source.title != target.title || source.developer != target.developer || source.summary != target.summary))) return false;
    const bool adoptBinding = bound(source) && !bound(target);
    const bool useText = !target.manualMetadata && (source.manualMetadata || adoptBinding);
    // 保留原始记录并登记别名；只向展示条目补入已有绑定和人工编辑，不改 Steam 统计。
    auto copy=prepare(db_.handle(),R"SQL(
        UPDATE game_resource AS t SET
          title=CASE WHEN ?1 THEN s.title ELSE t.title END,
          developer=CASE WHEN ?1 THEN s.developer ELSE t.developer END,
          summary=CASE WHEN ?1 THEN s.summary ELSE t.summary END,
          manual_metadata=t.manual_metadata OR s.manual_metadata,
          subject_id=COALESCE(t.subject_id,s.subject_id),
          vndb_id=CASE WHEN t.vndb_id='' THEN s.vndb_id ELSE t.vndb_id END,
          platform=CASE WHEN ?2 AND s.platform<>'' THEN s.platform ELSE t.platform END,
          metadata_source=CASE WHEN ?2 THEN s.metadata_source ELSE t.metadata_source END,
          cover=CASE WHEN ?3 THEN s.cover ELSE t.cover END,
          cover_mime=CASE WHEN ?3 THEN s.cover_mime ELSE t.cover_mime END
        FROM game_resource AS s WHERE s.id=?4 AND t.id=?5)SQL");
    sqlite3_bind_int(copy.get(),1,useText); sqlite3_bind_int(copy.get(),2,adoptBinding);
    sqlite3_bind_int(copy.get(),3,source.hasCover && (adoptBinding || !target.hasCover));
    sqlite3_bind_int64(copy.get(),4,source.id); sqlite3_bind_int64(copy.get(),5,target.id);
    done(copy);
    auto flatten=prepare(db_.handle(),"UPDATE game_import_alias SET game_id=? WHERE game_id=?");
    sqlite3_bind_int64(flatten.get(),1,target.id); sqlite3_bind_int64(flatten.get(),2,source.id); done(flatten);
    auto alias=prepare(db_.handle(),"INSERT INTO game_import_alias(alias_id,game_id) VALUES(?,?)");
    sqlite3_bind_int64(alias.get(),1,source.id); sqlite3_bind_int64(alias.get(),2,target.id); done(alias);
    return true;
}
void SqliteGameRepository::linkDuplicates() {
    for (;;) {
        auto games=list();
        std::sort(games.begin(),games.end(),[](const auto& a,const auto& b){return a.id < b.id;});
        bool changed=false;
        for (const auto& game : games) {
            if (!game.steamAppId.empty()) continue;
            const auto path=filePath(game.path);
            if (const auto* owner=steamOwner(path,games)) changed=link(game,*owner);
            else for (const auto& other : games) {
                if (other.id >= game.id) break;
                if (other.steamAppId.empty() && sameFile(path,filePath(other.path)) && link(game,other)) {
                    changed=true; break;
                }
            }
            if (changed) break;
        }
        if (!changed) return;
    }
}
void SqliteGameRepository::reconcileImportPaths() {
    std::lock_guard lock(db_.mutex());
    sqlite_support::Transaction transaction(db_.handle());
    linkDuplicates();
    transaction.commit();
}
std::int64_t SqliteGameRepository::importExecutable(const std::string& path, const std::string& title, bool* reused) {
    std::lock_guard lock(db_.mutex());
    sqlite_support::Transaction transaction(db_.handle());
    auto original = prepare(db_.handle(), "SELECT id FROM game_resource WHERE path=?");
    bindText(original, 1, path);
    const int exact = sqlite3_step(original.get());
    if (exact == SQLITE_ROW) {
        const auto id = canonicalId(sqlite3_column_int64(original.get(), 0));
        original.reset();
        transaction.commit();
        if (reused) *reused = true;
        return id;
    }
    if (exact != SQLITE_DONE) throw GameError("game_storage_error");

    const auto exe = filePath(path);
    const auto games = list();
    std::optional<std::int64_t> target;
    if (const auto* owner = steamOwner(exe, games)) {
        target = owner->id;
    } else {
        // 已隐藏的手动路径也能证明硬链接指向同一文件；出现多位所有者时保留单独条目。
        auto paths = prepare(db_.handle(), R"SQL(
            SELECT COALESCE(a.game_id,g.id),g.path FROM game_resource g
            LEFT JOIN game_import_alias a ON a.alias_id=g.id WHERE g.steam_app_id=''
        )SQL");
        int state;
        std::set<std::int64_t> owners;
        while ((state = sqlite3_step(paths.get())) == SQLITE_ROW) {
            if (sameFile(exe, filePath(text(paths, 1)))) owners.insert(sqlite3_column_int64(paths.get(), 0));
        }
        if (state != SQLITE_DONE) throw GameError("game_storage_error");
        if (owners.size() == 1) target = *owners.begin();
    }

    auto insert = prepare(db_.handle(), "INSERT INTO game_resource(path,title) VALUES(?,?)");
    bindText(insert, 1, path);
    bindText(insert, 2, title);
    done(insert);
    const auto id = sqlite3_last_insert_rowid(db_.handle());
    if (target) {
        auto alias = prepare(db_.handle(), "INSERT INTO game_import_alias(alias_id,game_id) VALUES(?,?)");
        sqlite3_bind_int64(alias.get(), 1, id);
        sqlite3_bind_int64(alias.get(), 2, *target);
        done(alias);
    }
    transaction.commit();
    if (reused) *reused = target.has_value();
    return target.value_or(id);
}
std::int64_t SqliteGameRepository::importSteam(const GameResource& game) {
    std::vector<GameResource> entries{game};
    importSteamBatch(entries);
    return entries.front().id;
}
std::size_t SqliteGameRepository::importSteamBatch(std::vector<GameResource>& entries) {
    if (entries.empty()) return 0;
    std::lock_guard lock(db_.mutex());
    sqlite_support::Transaction transaction(db_.handle());
    const auto previous = list();
    for (auto& game : entries) game.id = upsertSteam(game);
    // 必须先看到整批清单：多个 AppID 可能共享同一安装目录。
    linkDuplicates();
    std::set<std::int64_t> previousIds;
    for (const auto& game : previous) previousIds.insert(canonicalId(game.id));
    const auto updated = std::count_if(entries.begin(), entries.end(), [&](const auto& game) {
        return previousIds.contains(game.id);
    });
    transaction.commit();
    return updated;
}
std::int64_t SqliteGameRepository::upsertSteam(const GameResource& game) {
    // 同账号失败保留最后成功值；切换账号不能沿用上一个人的统计。
    auto statement = prepare(db_.handle(), R"SQL(
        INSERT INTO game_resource(path,title,platform,steam_app_id,steam_install_path,steam_manifest_path,
            steam_executable,steam_id,steam_stats_error,playtime_minutes,achievements_unlocked,achievements_total)
        VALUES(?,?,'Windows',?,?,?,?,?,?,?,?,?)
        ON CONFLICT(path) DO UPDATE SET
            steam_install_path=excluded.steam_install_path,steam_manifest_path=excluded.steam_manifest_path,
            steam_executable=excluded.steam_executable,steam_stats_error=excluded.steam_stats_error,
            playtime_minutes=CASE WHEN steam_id=excluded.steam_id THEN COALESCE(excluded.playtime_minutes,playtime_minutes) ELSE excluded.playtime_minutes END,
            achievements_unlocked=CASE WHEN steam_id=excluded.steam_id THEN COALESCE(excluded.achievements_unlocked,achievements_unlocked) ELSE excluded.achievements_unlocked END,
            achievements_total=CASE WHEN steam_id=excluded.steam_id THEN COALESCE(excluded.achievements_total,achievements_total) ELSE excluded.achievements_total END,
            steam_id=excluded.steam_id
    )SQL");
    bindText(statement, 1, game.path); bindText(statement, 2, game.title);
    bindText(statement, 3, game.steamAppId); bindText(statement, 4, game.steamInstallPath);
    bindText(statement, 5, game.steamManifestPath); bindText(statement, 6, game.steamExecutable);
    bindText(statement, 7, game.steamId); bindText(statement, 8, game.steamStatsError);
    const auto bindOptional = [&](int index, std::optional<std::int64_t> value) {
        if (value) sqlite3_bind_int64(statement.get(), index, *value); else sqlite3_bind_null(statement.get(), index);
    };
    bindOptional(9, game.playtimeMinutes); bindOptional(10, game.achievementsUnlocked); bindOptional(11, game.achievementsTotal);
    done(statement);
    auto existing = prepare(db_.handle(), "SELECT id FROM game_resource WHERE steam_app_id=?");
    bindText(existing, 1, game.steamAppId);
    if (sqlite3_step(existing.get()) != SQLITE_ROW) throw GameError("game_storage_error");
    const auto id=sqlite3_column_int64(existing.get(),0);
    return id;
}
void SqliteGameRepository::updateSteamStats(const GameResource& game) {
    std::lock_guard lock(db_.mutex());
    // 异步回调只能更新仍存在且账号一致的记录，不能复活用户删除的词条。
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET playtime_minutes=COALESCE(?,playtime_minutes),achievements_unlocked=COALESCE(?,achievements_unlocked),achievements_total=COALESCE(?,achievements_total),steam_stats_error=? WHERE id=? AND steam_id=? AND steam_app_id=?");
    const auto bindOptional = [&](int index, std::optional<std::int64_t> value) {
        if (value) sqlite3_bind_int64(statement.get(), index, *value); else sqlite3_bind_null(statement.get(), index);
    };
    bindOptional(1, game.playtimeMinutes); bindOptional(2, game.achievementsUnlocked); bindOptional(3, game.achievementsTotal);
    bindText(statement, 4, game.steamStatsError); sqlite3_bind_int64(statement.get(), 5, canonicalId(game.id));
    bindText(statement, 6, game.steamId); bindText(statement, 7, game.steamAppId); done(statement);
}
bool SqliteGameRepository::pathUsedByOther(const std::string& path, std::int64_t id) const {
    std::lock_guard lock(db_.mutex()); id = canonicalId(id);
    auto duplicate = prepare(db_.handle(), "SELECT id FROM game_resource WHERE path=? AND id<>?");
    bindText(duplicate, 1, path); sqlite3_bind_int64(duplicate.get(), 2, id);
    return sqlite3_step(duplicate.get()) == SQLITE_ROW;
}
void SqliteGameRepository::updateSteamMetadata(std::int64_t id, const std::string& appId, const std::string& chineseTitle, const std::optional<CachedCover>& cover) {
    if (chineseTitle.empty() && !cover) return;
    std::lock_guard lock(db_.mutex());
    // 回调只能更新同一 Steam 条目；人工标题及其他刮削来源优先，失败不清除旧封面。
    auto statement = prepare(db_.handle(), R"SQL(UPDATE game_resource SET
        title=CASE WHEN manual_metadata=0 AND ?<>'' THEN ? ELSE title END,
        cover=COALESCE(?,cover),cover_mime=CASE WHEN ?<>'' THEN ? ELSE cover_mime END,
        metadata_source='steam' WHERE id=? AND steam_app_id=? AND metadata_source IN ('','steam'))SQL");
    bindText(statement, 1, chineseTitle); bindText(statement, 2, chineseTitle);
    if (cover) sqlite3_bind_blob(statement.get(), 3, cover->bytes.data(), static_cast<int>(cover->bytes.size()), SQLITE_TRANSIENT);
    else sqlite3_bind_null(statement.get(), 3);
    bindText(statement, 4, cover ? cover->mimeType : ""); bindText(statement, 5, cover ? cover->mimeType : "");
    sqlite3_bind_int64(statement.get(), 6, canonicalId(id)); bindText(statement, 7, appId); done(statement);
}
void SqliteGameRepository::updateMetadata(std::int64_t id, const std::string& title, const std::string& developer, const std::string& summary, const std::string& path) {
    std::lock_guard lock(db_.mutex()); id = canonicalId(id);
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=?,developer=?,summary=?,path=?,manual_metadata=1 WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, path); sqlite3_bind_int64(statement.get(), 5, id); done(statement);
}
void SqliteGameRepository::remove(std::int64_t id) {
    std::lock_guard lock(db_.mutex()); id = canonicalId(id);
    sqlite_support::Transaction tx(db_.handle());
    auto copies=prepare(db_.handle(),"DELETE FROM game_resource WHERE id IN (SELECT alias_id FROM game_import_alias WHERE game_id=?)");
    sqlite3_bind_int64(copies.get(),1,id); done(copies);
    auto statement = prepare(db_.handle(), "DELETE FROM game_resource WHERE id=?");
    sqlite3_bind_int64(statement.get(), 1, id); done(statement); tx.commit();
}
void SqliteGameRepository::bind(std::int64_t id, std::int64_t subjectId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) {
    std::lock_guard lock(db_.mutex()); id = canonicalId(id);
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=CASE WHEN manual_metadata=0 THEN ? ELSE title END,developer=CASE WHEN manual_metadata=0 THEN ? ELSE developer END,summary=CASE WHEN manual_metadata=0 THEN ? ELSE summary END,platform=?,cover=CASE WHEN subject_id=? AND metadata_source='bangumi' THEN cover ELSE NULL END,cover_mime=CASE WHEN subject_id=? AND metadata_source='bangumi' THEN cover_mime ELSE '' END,subject_id=?,metadata_source='bangumi' WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, platform);
    for (int index = 5; index <= 7; ++index) sqlite3_bind_int64(statement.get(), index, subjectId);
    sqlite3_bind_int64(statement.get(), 8, id); done(statement);
}
void SqliteGameRepository::bindVndb(std::int64_t id, const std::string& vndbId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) {
    std::lock_guard lock(db_.mutex()); id = canonicalId(id);
    // 两个站点的 ID 独立保留，封面只属于当前选中的刮削来源。
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=CASE WHEN manual_metadata=0 THEN ? ELSE title END,developer=CASE WHEN manual_metadata=0 THEN ? ELSE developer END,summary=CASE WHEN manual_metadata=0 THEN ? ELSE summary END,platform=?,cover=CASE WHEN vndb_id=? AND metadata_source='vndb' THEN cover ELSE NULL END,cover_mime=CASE WHEN vndb_id=? AND metadata_source='vndb' THEN cover_mime ELSE '' END,vndb_id=?,metadata_source='vndb' WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, platform);
    for (int index = 5; index <= 7; ++index) bindText(statement, index, vndbId);
    sqlite3_bind_int64(statement.get(), 8, id); done(statement);
}
bool SqliteGameRepository::setCover(std::int64_t id, std::int64_t subjectId, const std::string& bytes, const std::string& mime) {
    std::lock_guard lock(db_.mutex()); id = canonicalId(id);
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET cover=?,cover_mime=? WHERE id=? AND subject_id=? AND metadata_source='bangumi'");
    sqlite3_bind_blob(statement.get(), 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT); bindText(statement, 2, mime);
    sqlite3_bind_int64(statement.get(), 3, id); sqlite3_bind_int64(statement.get(), 4, subjectId); done(statement); return sqlite3_changes(db_.handle()) != 0;
}
std::optional<CachedCover> SqliteGameRepository::cover(std::int64_t id) const {
    std::lock_guard lock(db_.mutex()); id = canonicalId(id); auto statement = prepare(db_.handle(), "SELECT cover,cover_mime FROM game_resource WHERE id=?"); sqlite3_bind_int64(statement.get(), 1, id);
    if (sqlite3_step(statement.get()) != SQLITE_ROW || sqlite3_column_type(statement.get(), 0) == SQLITE_NULL) return {};
    CachedCover result; result.bytes.assign(static_cast<const char*>(sqlite3_column_blob(statement.get(), 0)), sqlite3_column_bytes(statement.get(), 0)); result.mimeType = text(statement, 1); return result;
}
bool SqliteGameRepository::setVndbCover(std::int64_t id, const std::string& vndbId, const std::string& bytes, const std::string& mime) {
    std::lock_guard lock(db_.mutex()); id = canonicalId(id); auto statement = prepare(db_.handle(), "UPDATE game_resource SET cover=?,cover_mime=? WHERE id=? AND vndb_id=? AND metadata_source='vndb'");
    sqlite3_bind_blob(statement.get(), 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT); bindText(statement, 2, mime);
    sqlite3_bind_int64(statement.get(), 3, id); bindText(statement, 4, vndbId); done(statement); return sqlite3_changes(db_.handle()) != 0;
}
}
