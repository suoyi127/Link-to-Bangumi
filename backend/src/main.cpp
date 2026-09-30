#include "anime_vault/api/HealthController.hpp"
#include "anime_vault/api/MediaController.hpp"
#include "anime_vault/api/QbConfigController.hpp"
#include "anime_vault/api/BangumiConfigController.hpp"
#include "anime_vault/api/NovelController.hpp"
#include "anime_vault/api/GameController.hpp"
#include "anime_vault/api/MediaService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
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
        anime_vault::PlaybackService playback(repository, source, imported, library, processLauncher);
        const char* configuredAgent = std::getenv("ANIME_VAULT_BANGUMI_USER_AGENT");
        auto bangumiConnection = std::make_shared<anime_vault::BangumiConnectionManager>(
            database, configuredAgent ? configuredAgent : "");
        auto bangumi = std::make_shared<anime_vault::BangumiService>(repository, *bangumiConnection);
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
        anime_vault::api::registerManagementEndpoints(repository,
            {source, imported, library, data,
             bangumiConnection->summary().configured, qb->summary().configured,
             paths.qbConfigured, sourceOverride.has_value(),
             [qb] { return qb->summary().configured; },
             [bangumiConnection] { return bangumiConnection->summary().configured; }});
        anime_vault::api::registerQbConfigEndpoints(qb);
        anime_vault::api::registerBangumiConfigEndpoints(bangumiConnection);
        auto novels = std::make_shared<anime_vault::NovelService>(database);
        auto novelBangumi = std::make_shared<anime_vault::NovelBangumiService>(*novels, *bangumiConnection, *bangumiConnection);
        anime_vault::api::registerNovelEndpoints(novels, novelBangumi, processLauncher);
        auto games = std::make_shared<anime_vault::GameService>(database);
        auto vndb = std::make_shared<anime_vault::DrogonVndbTransport>();
        auto gameBangumi = std::make_shared<anime_vault::GameBangumiService>(*games, *bangumiConnection, *bangumiConnection, vndb.get());
        anime_vault::api::registerGameEndpoints(games, gameBangumi, processLauncher);
        auto calendar = std::make_shared<anime_vault::BangumiCalendarService>(repository, *bangumiConnection);
        anime_vault::api::registerAnimeEndpoints(repository, bangumi, covers, calendar);
        drogon::app().registerHandler("/api/qb/status", [qb](const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            qb->current()->inspect([callback = std::move(callback)](anime_vault::QbStatus status) mutable {
                Json::Value payload;
                payload["configured"] = status.configured;
                payload["connected"] = status.connected;
                payload["errorCode"] = status.errorCode;
                payload["version"] = status.version;
                payload["torrentCount"] = status.torrentCount;
                payload["completedCount"] = status.completedCount;
                callback(drogon::HttpResponse::newHttpJsonResponse(payload));
            });
        }, {drogon::Get});
        drogon::app().registerHandler("/api/qb/rss", [qb](const drogon::HttpRequestPtr&,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            qb->current()->readMikanTitles([callback = std::move(callback)](anime_vault::QbMikanCatalog catalog) mutable {
                Json::Value payload;
                payload["feedCount"] = catalog.feedCount;
                payload["articleCount"] = catalog.articleCount;
                payload["pairCount"] = Json::UInt64(catalog.pairs.size());
                payload["errorCode"] = catalog.errorCode;
                payload["feeds"] = Json::Value(Json::arrayValue);
                for (const auto& feed : catalog.feeds) {
                    Json::Value item;
                    item["title"] = feed.title;
                    item["articleCount"] = feed.articleCount;
                    item["hasError"] = feed.hasError;
                    payload["feeds"].append(item);
                }
                callback(drogon::HttpResponse::newHttpJsonResponse(payload));
            });
        }, {drogon::Get});
        drogon::app().registerHandler("/api/qb/rss/feeds", [qb, source, qbDownloadError, &media](const drogon::HttpRequestPtr& request,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto body = request->body().size() <= 2048 ? request->getJsonObject() : nullptr;
            if (!body || !body->isObject() || body->size() != 1 || !(*body)["url"].isString() ||
                !anime_vault::QbWebClient::validMikanFeedUrl((*body)["url"].asString())) {
                Json::Value payload;
                payload["error"]["code"] = "invalid_mikan_feed_url";
                payload["error"]["message"] = "a Mikan HTTPS RSS URL is required";
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                response->setStatusCode(drogon::k400BadRequest);
                callback(response);
                return;
            }
            if (const auto error = qbDownloadError(); !error.empty()) {
                Json::Value payload;
                payload["error"]["code"] = error;
                payload["error"]["message"] = "restart or configure the qB download directory";
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                response->setStatusCode(drogon::k409Conflict);
                callback(response);
                return;
            }
            try { media.validateQbSourceReady(); }
            catch (const anime_vault::api::ApiError& error) {
                Json::Value payload;
                payload["error"]["code"] = error.code;
                payload["error"]["message"] = error.what();
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                response->setStatusCode(static_cast<drogon::HttpStatusCode>(error.status));
                callback(response);
                return;
            }
            const auto path = std::filesystem::absolute(source).u8string();
            qb->current()->addMikanFeedWithAutoRule((*body)["url"].asString(),
                std::string(reinterpret_cast<const char*>(path.data()), path.size()),
                [callback = std::move(callback)](anime_vault::QbActionResult result) mutable {
                    Json::Value payload;
                    payload["success"] = result.success;
                    payload["errorCode"] = result.errorCode;
                    if (!result.success) {
                        payload["error"]["code"] = result.errorCode;
                        payload["error"]["message"] = "qB RSS feed action failed";
                    }
                    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                    if (!result.success) response->setStatusCode(
                        (result.errorCode == "qb_feed_exists_or_invalid" ||
                         result.errorCode == "qb_rule_exists") ? drogon::k409Conflict :
                        drogon::k503ServiceUnavailable);
                    callback(response);
                });
        }, {drogon::Post});
        drogon::app().registerHandler("/api/qb/rss/rules", [qb, source, qbDownloadError, &media](const drogon::HttpRequestPtr& request,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto body = request->body().size() <= 4096 ? request->getJsonObject() : nullptr;
            if (!body || !body->isObject() || body->size() != 3 ||
                !(*body)["ruleName"].isString() || !(*body)["feedUrl"].isString() ||
                !(*body)["keyword"].isString()) {
                Json::Value payload;
                payload["error"]["code"] = "invalid_mikan_rule";
                payload["error"]["message"] = "ruleName, feedUrl and keyword are required";
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                response->setStatusCode(drogon::k400BadRequest);
                callback(response);
                return;
            }
            if (const auto error = qbDownloadError(); !error.empty()) {
                Json::Value payload;
                payload["error"]["code"] = error;
                payload["error"]["message"] = "restart or configure the qB download directory";
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                response->setStatusCode(drogon::k409Conflict);
                callback(response);
                return;
            }
            try { media.validateQbSourceReady(); }
            catch (const anime_vault::api::ApiError& error) {
                Json::Value payload;
                payload["error"]["code"] = error.code;
                payload["error"]["message"] = error.what();
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                response->setStatusCode(static_cast<drogon::HttpStatusCode>(error.status));
                callback(response);
                return;
            }
            const auto path = std::filesystem::absolute(source).u8string();
            anime_vault::QbMikanRuleSpec spec{
                (*body)["ruleName"].asString(), (*body)["feedUrl"].asString(),
                (*body)["keyword"].asString(),
                std::string(reinterpret_cast<const char*>(path.data()), path.size())};
            qb->current()->createMikanRule(std::move(spec),
                [callback = std::move(callback)](anime_vault::QbActionResult result) mutable {
                    Json::Value payload;
                    payload["success"] = result.success;
                    payload["errorCode"] = result.errorCode;
                    if (!result.success) {
                        payload["error"]["code"] = result.errorCode;
                        payload["error"]["message"] = "qB RSS rule action failed";
                    }
                    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                    if (!result.success) response->setStatusCode(
                        result.errorCode == "invalid_mikan_rule" ? drogon::k400BadRequest :
                        result.errorCode == "qb_rule_exists" ? drogon::k409Conflict :
                        drogon::k503ServiceUnavailable);
                    callback(response);
                });
        }, {drogon::Post});
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
        drogon::app().addListener(std::string(anime_vault::api::kHealthBindAddress), port).run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
