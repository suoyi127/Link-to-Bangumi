#include "anime_vault/application/QbCompletedScanCoordinator.hpp"
#include "anime_vault/infrastructure/NativeLibraryFileAccess.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteNovelRepository.hpp"
#include "anime_vault/infrastructure/database/SqliteGameRepository.hpp"
#include "anime_vault/api/QbRssController.hpp"
#include "anime_vault/api/HealthController.hpp"
#include "anime_vault/api/MediaController.hpp"
#include "anime_vault/api/QbConfigController.hpp"
#include "anime_vault/api/BangumiConfigController.hpp"
#include "anime_vault/api/NovelController.hpp"
#include "anime_vault/api/GameController.hpp"
#include "anime_vault/api/SteamController.hpp"
#include "anime_vault/infrastructure/network/WindowsSystemHttp.hpp"
#include "anime_vault/api/MediaService.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"
#include "anime_vault/services/OrganizationService.hpp"
#include "anime_vault/services/BangumiService.hpp"
#include "anime_vault/services/AnimeEnricher.hpp"
#include "anime_vault/services/MikanEnricher.hpp"
#include "anime_vault/infrastructure/network/DrogonBangumiTransport.hpp"
#include "anime_vault/infrastructure/network/DrogonVndbTransport.hpp"
#include "anime_vault/infrastructure/network/QbWebClient.hpp"
#include "anime_vault/infrastructure/network/QbConnectionManager.hpp"
#include "anime_vault/infrastructure/network/DrogonCoverTransport.hpp"
#include "anime_vault/infrastructure/network/BangumiConnectionManager.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include "anime_vault/services/PlaybackService.hpp"
#include "anime_vault/services/MpvProgressMonitor.hpp"
#include "anime_vault/infrastructure/database/WatchProgressStore.hpp"
#include "anime_vault/api/WatchProgressController.hpp"
#include "anime_vault/api/WatchSyncController.hpp"
#include "anime_vault/services/WatchSyncService.hpp"
#include "anime_vault/services/RuntimePaths.hpp"
#include "anime_vault/services/RuntimeWebRoot.hpp"

#include <drogon/drogon.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <filesystem>
#include <string>
#include <memory>
#include <optional>

int main() {
    try {
        // 先解析持久化与媒体目录，再组装服务；设置页保存的路径由仓库偏好提供。
        const auto environmentPath = [](const char* name, const std::filesystem::path& fallback) {
            const char* value = std::getenv(name);
            return std::filesystem::path(value && *value ? value : fallback);
        };
        const auto data = environmentPath("ANIME_VAULT_DATA_DIR", anime_vault::defaultDataDirectory());
        std::filesystem::create_directories(data);
        anime_vault::SqliteDatabase database(data / "anime-vault.db");
        database.migrate();
        anime_vault::SqliteMediaRepository repository(database);
        const auto mediaRoot = anime_vault::defaultMediaDirectory();
        std::optional<std::filesystem::path> sourceOverride;
        if (const char* value = std::getenv("ANIME_VAULT_SOURCE_DIR"); value && *value)
            sourceOverride = std::filesystem::path(value);
        auto paths = anime_vault::resolveRuntimePaths(data,
            environmentPath("ANIME_VAULT_IMPORT_DIR", mediaRoot / "Import"),
            environmentPath("ANIME_VAULT_LIBRARY_DIR", mediaRoot / "Library"),
            sourceOverride, repository.getUiPreferences().qbDownloadDirectory);
        // A removed drive must not prevent the desktop app from opening; keep the saved choice pending.
        if (paths.qbConfigured && !std::filesystem::is_directory(paths.source)) {
            paths.source = paths.data / "unconfigured-qb-source";
            paths.qbConfigured = false;
        }
        const auto& source = paths.source;
        const auto& library = paths.library;
        const auto& imported = paths.imported;
        if (!paths.qbConfigured) std::filesystem::create_directories(source);
        anime_vault::api::MediaService media(repository,
            source, library, imported, paths.qbConfigured);
        std::filesystem::create_directories(imported);
        std::filesystem::create_directories(library);
        anime_vault::OrganizationService organization(repository, source, imported, library);
        anime_vault::NativeProcessLauncher processLauncher;
        anime_vault::WatchProgressStore watchStore(database);
        anime_vault::MpvProgressMonitor monitor(watchStore);
        anime_vault::PlaybackService playback(repository, source, imported, library, processLauncher, &monitor);
        const char* configuredAgent = std::getenv("ANIME_VAULT_BANGUMI_USER_AGENT");
        auto bangumiConnection = std::make_shared<anime_vault::BangumiConnectionManager>(
            database, configuredAgent ? configuredAgent : "");
        auto bangumi = std::make_shared<anime_vault::BangumiService>(repository, *bangumiConnection,
            [] { return std::chrono::system_clock::now(); },
            [](std::chrono::milliseconds delay, std::function<void()> action) {
                drogon::app().getLoop()->runAfter(static_cast<double>(delay.count()) / 1000.0, std::move(action));
            });
        auto covers = std::make_shared<anime_vault::CoverScraper>(
            repository, bangumi, *bangumiConnection, data);
        auto enricher = std::make_shared<anime_vault::AnimeEnricher>(repository, bangumi, covers);
        const auto environmentString = [](const char* name) {
            const char* value = std::getenv(name);
            return std::string(value ? value : "");
        };
        auto qb = std::make_shared<anime_vault::QbConnectionManager>(data,
            anime_vault::QbConnectionConfig{"http://[::1]:8080",
                environmentString("ANIME_VAULT_QB_USERNAME"),
                environmentString("ANIME_VAULT_QB_PASSWORD")});
        auto mikanEnricher = std::make_shared<anime_vault::MikanEnricher>(repository,
            [qb](anime_vault::MikanEnricher::CatalogCompletion completion) {
                qb->current()->readMikanTitles(std::move(completion));
            });
        const auto qbDownloadError = [&repository, source, configured = paths.qbConfigured,
                                      environmentOverride = sourceOverride.has_value()] {
            if (!configured) return std::string("qb_download_dir_unconfigured");
            return anime_vault::qbDirectoryActive(source,
                repository.getUiPreferences().qbDownloadDirectory, environmentOverride)
                ? std::string{} : std::string("qb_download_dir_restart_required");
        };
        const auto port = anime_vault::api::resolveHealthPort(std::getenv("ANIME_VAULT_PORT"));
        // HTTP 控制器只负责协议适配，扫描、整理、刮削等规则留在独立服务中。
        anime_vault::api::registerHealthEndpoint(
            anime_vault::api::resolveInstanceToken(std::getenv("ANIME_VAULT_INSTANCE_TOKEN")));
        anime_vault::api::registerMediaEndpoints(media, organization, enricher, mikanEnricher);
        anime_vault::api::registerPlaybackEndpoint(repository, playback);
        anime_vault::api::registerWatchProgressEndpoints(watchStore);
        auto watchSync=anime_vault::makeWatchSyncService(database,data,[bangumiConnection] {
            return bangumiConnection->summary().userAgent;
        });
        struct StopWatchSync {
            std::shared_ptr<anime_vault::WatchSyncService> service;
            ~StopWatchSync() { service->stop(); }
        } stopWatchSync{watchSync};
        anime_vault::api::registerWatchSyncEndpoints(watchSync);
        anime_vault::api::registerManagementEndpoints(repository,
            {source, imported, library, data,
             bangumiConnection->summary().configured, qb->summary().configured,
             paths.qbConfigured, sourceOverride.has_value(),
             [qb] { return qb->summary().configured; },
             [bangumiConnection] { return bangumiConnection->summary().configured; }});
        anime_vault::api::registerQbConfigEndpoints(qb);
        anime_vault::api::registerBangumiConfigEndpoints(bangumiConnection);
        anime_vault::SqliteNovelRepository novelsRepository(database);
        anime_vault::NativeLibraryFileAccess libraryFiles;
        auto novels = std::make_shared<anime_vault::NovelService>(novelsRepository, libraryFiles);
        auto novelBangumi = std::make_shared<anime_vault::NovelBangumiService>(*novels, *bangumiConnection, *bangumiConnection);
        anime_vault::api::registerNovelEndpoints(novels, novelBangumi, processLauncher);
        anime_vault::SqliteGameRepository gamesRepository(database);
        gamesRepository.reconcileImportPaths();
        auto games = std::make_shared<anime_vault::GameService>(gamesRepository, libraryFiles);
        auto vndb = std::make_shared<anime_vault::DrogonVndbTransport>();
        auto gameBangumi = std::make_shared<anime_vault::GameBangumiService>(*games, *bangumiConnection, *bangumiConnection, vndb.get());
        anime_vault::api::registerGameEndpoints(games, gameBangumi, processLauncher);
        auto steamConfig = std::make_shared<anime_vault::SteamConfiguration>(data);
        auto steamClient = std::make_shared<anime_vault::SteamClient>([](std::string path, auto callback) {
#ifdef _WIN32
            anime_vault::windowsSystemHttpsRequest("api.steampowered.com", std::move(path), "GET",
                "LinkToBangumi/0.1.2", "", 8 * 1024 * 1024,
                [callback = std::move(callback)](anime_vault::WindowsHttpResult result) mutable {
                    callback(result.status, result.body.value_or(""));
                });
#else
            (void)path; callback(0, "");
#endif
        });
        auto steamMetadata = std::make_shared<anime_vault::SteamMetadataClient>([](std::string host, std::string path, auto callback) {
#ifdef _WIN32
            const auto limit = host == "store.steampowered.com" ? 2 * 1024 * 1024 : 8 * 1024 * 1024;
            anime_vault::windowsSystemHttpsRequest(std::move(host), std::move(path), "GET", "LinkToBangumi/0.1.2", "", limit,
                [callback = std::move(callback)](anime_vault::WindowsHttpResult result) mutable {
                    callback(result.status, result.body.value_or(""));
                });
#else
            (void)host; (void)path; callback(0, "");
#endif
        });
        auto steamImports = std::make_shared<anime_vault::SteamImportService>(gamesRepository, *steamClient, steamMetadata.get());
        struct StopSteamImports {
            std::shared_ptr<anime_vault::SteamImportService> service;
            ~StopSteamImports() { service->stop(); }
        } stopSteamImports{steamImports};
        auto steamLogin = std::make_shared<anime_vault::SteamLoginService>(processLauncher,
            mediaRoot.parent_path(), data / "SteamLogin",
            [steamConfig](std::string id, std::string key) {
                auto value = steamConfig->current(); value.steamId = std::move(id); value.apiKey = std::move(key);
                steamConfig->save(std::move(value));
            }, [steamImports] { return steamImports->busy(); });
        anime_vault::api::registerSteamEndpoints(steamConfig, steamImports, steamLogin);
        auto calendar = std::make_shared<anime_vault::BangumiCalendarService>(repository, *bangumiConnection);
        anime_vault::api::registerAnimeEndpoints(repository, bangumi, covers, calendar);
        const auto sourceUtf8 = source.u8string();
        auto downloads = std::make_shared<anime_vault::MikanDownloadWorker>(database, qb,
            std::string(sourceUtf8.begin(), sourceUtf8.end()), [qbDownloadError, &media] {
                if (auto code = qbDownloadError(); !code.empty()) throw anime_vault::QbConfigError(code, code);
                media.validateQbSourceReady();
            });
        anime_vault::api::registerQbRssEndpoints(qb, source, qbDownloadError,
            [&media] { media.validateQbSourceReady(); }, downloads);
        if (const char* webDirectory = std::getenv("ANIME_VAULT_WEB_DIR"); webDirectory && *webDirectory) {
            const auto* bytes = reinterpret_cast<const char8_t*>(webDirectory);
            const auto root = anime_vault::validatedWebRoot(
                std::filesystem::path(std::u8string_view(bytes, std::char_traits<char>::length(webDirectory))));
            const auto encoded = root.u8string();
            // Static files are enabled only for an explicit production bundle root.
            drogon::app().setDocumentRoot(
                std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
            drogon::app().setHomePage("index.html");
        }
        // qB 完成扫描在独立线程运行，主入口仅组装应用依赖。
        anime_vault::QbCompletedScanCoordinator automaticScan({
            [qb, qbDownloadError] { return qbDownloadError().empty() && qb->summary().configured; },
            [qb](auto completion) { qb->current()->inspect(std::move(completion)); },
            [&media](const auto& completed) { return media.createCompletedScan(completed); },
            [](auto work) { drogon::app().getLoop()->queueInLoop(std::move(work)); },
            [enricher, mikanEnricher](std::int64_t scanId, auto done) {
                mikanEnricher->runOnce([enricher, scanId, done = std::move(done)](anime_vault::MikanEnrichmentResult) mutable {
                    auto completed = [done = std::move(done)](std::size_t) { done(); };
                    if (scanId > 0) enricher->runForScan(scanId, std::move(completed));
                    else enricher->runOnce(5, std::move(completed));
                });
            },
            [](const auto& message) { std::cerr << message << '\n'; }
        });
        automaticScan.start();
        drogon::app().addListener(std::string(anime_vault::api::kHealthBindAddress), port).run();
        downloads->stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
