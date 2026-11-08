#include "anime_vault/services/SteamImportService.hpp"
#include "anime_vault/services/SteamLibrary.hpp"
#include <set>
namespace anime_vault {
namespace {
std::filesystem::path fromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
}
void SteamImportService::importInstalled(SteamConfig config, Completion completion) {
    std::lock_guard lifecycle(lifecycle_);
    if (stopped_) { completion({0, 0, {}, "steam_import_stopped"}); return; }
    if (busy_.exchange(true)) { completion({0, 0, {}, "steam_import_in_progress"}); return; }
    SteamImportResult result;
    std::vector<GameResource> entries;
    try {
        SteamConfiguration::validate(config);
        const auto scanned = SteamLibrary::scan(config.steamPath.empty() ? SteamLibrary::detectRoot() : fromUtf8(config.steamPath));
        result.warnings = scanned.warnings;
        // 先落库本地结果，网络失败不会丢失已发现的启动地址。
        for (const auto& installed : scanned.games) {
            GameResource game;
            game.steamAppId = installed.appId; game.title = installed.title;
            game.path = "steam://rungameid/" + installed.appId;
            game.steamInstallPath = installed.installPath; game.steamManifestPath = installed.manifestPath;
            game.steamExecutable = installed.steamExecutable; game.steamId = config.steamId;
            game.steamStatsError = "steam_stats_pending";
            entries.push_back(std::move(game));
        }
        result.updated = static_cast<int>(games_.importSteamBatch(entries));
        result.imported = static_cast<int>(entries.size()) - result.updated;
    } catch (const GameError& error) { result.errorCode = error.code; }
      catch (...) { result.errorCode = "steam_import_failed"; }
    if (!result.errorCode.empty() || entries.empty()) { busy_ = false; completion(std::move(result)); return; }
    std::vector<std::string> ids;
    for (const auto& entry : entries) ids.push_back(entry.steamAppId);
    auto self = shared_from_this();
    client_.fetch(std::move(config), std::move(ids),
        [self, entries = std::move(entries), result = std::move(result), completion = std::move(completion)](auto stats) mutable {
            std::lock_guard lifecycle(self->lifecycle_);
            if (self->stopped_) return;
            std::set<std::string> warnings(result.warnings.begin(), result.warnings.end());
            try {
                for (auto& game : entries) {
                    const auto found = stats.find(game.steamAppId);
                    game.steamStatsError = "steam_stats_unavailable";
                    if (found != stats.end()) {
                        game.playtimeMinutes = found->second.playtimeMinutes;
                        game.achievementsUnlocked = found->second.achievementsUnlocked;
                        game.achievementsTotal = found->second.achievementsTotal;
                        game.steamStatsError = found->second.errorCode;
                    }
                    if (!game.steamStatsError.empty()) warnings.insert(game.steamStatsError);
                    self->games_.updateSteamStats(game);
                }
            } catch (...) { result.errorCode = "game_storage_error"; }
            result.warnings.assign(warnings.begin(), warnings.end());
            self->enrich(std::move(entries), std::move(result), std::move(completion));
        });
}
void SteamImportService::enrich(std::vector<GameResource> entries, SteamImportResult result, Completion completion) {
    if (!metadata_ || !result.errorCode.empty()) { busy_ = false; completion(std::move(result)); return; }
    std::vector<std::string> ids;
    for (const auto& entry : entries) ids.push_back(entry.steamAppId);
    auto self = shared_from_this();
    // 公开名称和封面不依赖统计账号，也不因某款游戏的成就不可用而跳过。
    metadata_->fetch(std::move(ids), [self, entries = std::move(entries), result = std::move(result), completion = std::move(completion)](auto metadata) mutable {
        std::lock_guard lifecycle(self->lifecycle_);
        if (self->stopped_) return;
        std::set<std::string> warnings(result.warnings.begin(), result.warnings.end());
        try {
            for (const auto& entry : entries) {
                const auto found = metadata.find(entry.steamAppId);
                if (found == metadata.end()) continue;
                const auto& value = found->second;
                self->games_.updateSteamMetadata(entry.id, entry.steamAppId, value.chineseTitle, value.cover);
                warnings.insert(value.warnings.begin(), value.warnings.end());
            }
        } catch (...) { result.errorCode = "game_storage_error"; }
        result.warnings.assign(warnings.begin(), warnings.end());
        self->busy_ = false; completion(std::move(result));
    });
}
void SteamImportService::stop() {
    // 退出时先隔离后台回调，等待正在使用仓库的短临界区完成后再销毁数据库。
    std::lock_guard lifecycle(lifecycle_);
    stopped_ = true;
    busy_ = false;
}
}
