#include "anime_vault/api/ScanController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/api/MediaJson.hpp"

namespace anime_vault::api {
namespace {
InboxPageHttpRequest parseInboxPageRequest(const Request& request) {
    if (!request->body().empty()) throw ApiError(400, "invalid_request", "inbox takes no body");
    for (const auto& [name, _] : request->getParameters())
        if (name != "limit" && name != "offset" && name != "origin")
            throw ApiError(400, "invalid_page", "unknown page parameter");
    InboxPageHttpRequest dto;
    dto.limit = static_cast<int>(pageNumber(request, "limit", 100, 1, 100));
    dto.offset = pageNumber(request, "offset", 0, 0, 1'000'000);
    if (request->getParameters().contains("origin")) {
        dto.origin = request->getParameter("origin");
        if (*dto.origin != "qb_download" && *dto.origin != "external_import" &&
            *dto.origin != "folder_import")
            throw ApiError(400, "invalid_origin", "invalid inbox origin");
    }
    return dto;
}
void triggerScanEnrichment(const std::shared_ptr<AnimeEnricher>& enricher,
                           const std::shared_ptr<MikanEnricher>& mikanEnricher,
                           std::optional<std::int64_t> scanId = std::nullopt,
                           std::function<void()> completion = {}) {
    // 所有扫描来源先尝试 Mikan 双语别名，再用规范标题匹配 Bangumi 与封面。
    auto afterMikan = [enricher, scanId, completion = std::move(completion)](MikanEnrichmentResult) mutable {
        if (enricher) {
            auto finish = [completion = std::move(completion)](std::size_t) {
                if (completion) completion();
            };
            if (scanId) enricher->runForScan(*scanId, std::move(finish));
            else enricher->runOnce(5, std::move(finish));
        }
        else if (completion) completion();
    };
    if (mikanEnricher) mikanEnricher->runOnce(std::move(afterMikan));
    else afterMikan({});
}
}
void registerScanManagementEndpoints(MediaRepository& repository) {
    drogon::app().registerHandler("/api/scans", [&repository](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty()) throw ApiError(400, "invalid_request", "body not allowed");
            for (const auto& [name, _] : request->getParameters())
                if (name != "limit" && name != "offset") throw ApiError(400, "invalid_page", "unknown page parameter");
            const auto limit = static_cast<int>(pageNumber(request, "limit", 50, 1, 100));
            const auto offset = pageNumber(request, "offset", 0, 0, 1'000'000);
            const auto page = repository.listScanPage(offset, limit);
            Json::Value result;
            result["items"] = Json::Value(Json::arrayValue);
            for (const auto& item : page.items) result["items"].append(scanJson(item));
            result["nextOffset"] = page.nextOffset ? Json::Value(Json::Int64(*page.nextOffset)) : Json::Value(Json::nullValue);
            return result;
        });
    }, {drogon::Get});
}
void registerScanController(MediaService& service,
                            std::shared_ptr<AnimeEnricher> enricher,
                            std::shared_ptr<MikanEnricher> mikanEnricher) {
    drogon::app().registerHandler("/api/enrichment/sync", [enricher, mikanEnricher](
        const Request& request, Callback&& callback) {
        const auto requestId = requestIdFor(request);
        if (!request->body().empty() || !request->getParameters().empty()) {
            deliver(std::move(callback), requestId, 400,
                    errorJson("invalid_request", "metadata sync takes no body or parameters"));
            return;
        }
        auto finish = [enricher, requestId, callback = std::move(callback)](
            MikanEnrichmentResult mikan) mutable {
            auto report = [requestId, callback = std::move(callback), mikan = std::move(mikan)](
                std::size_t bangumiBound) mutable {
                Json::Value payload;
                payload["mikanApplied"] = Json::UInt64(mikan.appliedCount);
                payload["bangumiBound"] = Json::UInt64(bangumiBound);
                payload["mikanErrorCode"] = mikan.errorCode;
                deliver(std::move(callback), requestId, 200, std::move(payload));
            };
            if (enricher) enricher->runOnce(5, std::move(report));
            else report(0);
        };
        if (mikanEnricher) mikanEnricher->runOnce(std::move(finish));
        else finish({0, "mikan_unconfigured"});
    }, {drogon::Post});
    drogon::app().registerHandler("/api/scans", [&service, enricher, mikanEnricher](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "scan takes no body or parameters");
            const auto scan = service.createScan();
            if (scan.status == "completed") triggerScanEnrichment(enricher, mikanEnricher, scan.id);
            return scanJson(scan);
        }, 201);
    }, {drogon::Post});
    drogon::app().registerHandler("/api/imports/scan", [&service, enricher, mikanEnricher](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "import scan takes no body or parameters");
            const auto scan = service.createImportScan();
            if (scan.status == "completed") triggerScanEnrichment(enricher, mikanEnricher, scan.id);
            return scanJson(scan);
        }, 201);
    }, {drogon::Post});
    drogon::app().registerHandler("/api/folder-imports", [&service](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "folder list takes no body or parameters");
            Json::Value result;
            result["items"] = Json::Value(Json::arrayValue);
            for (const auto& folder : service.listFolderImports())
                result["items"].append(folderImportJson(folder));
            return result;
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/folder-imports", [&service](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (request->body().size() > 4096 || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "invalid folder import request");
            const auto body = request->getJsonObject();
            if (!body || !body->isObject() || body->size() != 1 || !(*body)["path"].isString())
                throw ApiError(400, "invalid_request", "path is required");
            return folderImportJson(service.addFolderImport((*body)["path"].asString()));
        }, 201);
    }, {drogon::Post});
    drogon::app().registerHandler("/api/folder-imports/{1}/scan", [&service, enricher, mikanEnricher](
        const Request& request, Callback&& callback, std::string id) {
        const auto requestId = requestIdFor(request);
        try {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "folder scan takes no body or parameters");
            const auto scan = service.createFolderScan(parseId(id));
            if (scan.status != "completed") {
                deliver(std::move(callback), requestId, 201, scanJson(scan));
                return;
            }
            deliver(std::move(callback), requestId, 201, scanJson(scan));
            triggerScanEnrichment(enricher, mikanEnricher, scan.id);
        } catch (const ApiError& error) {
            deliver(std::move(callback), requestId, error.status, errorJson(error.code, error.what()));
        } catch (const MediaError& error) {
            deliver(std::move(callback), requestId, mediaErrorStatus(error.kind), errorJson(error.code, error.what()));
        } catch (const std::exception&) {
            deliver(std::move(callback), requestId, 500, errorJson("internal_error", "internal error"));
        }
    }, {drogon::Post});
    drogon::app().registerHandler("/api/scans/{1}", [&service](const Request& request, Callback&& callback, std::string id) {
        respond(request, std::move(callback), [&] { return scanJson(service.getScan(parseId(id))); });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/inbox", [&service](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            const auto dto = parseInboxPageRequest(request);
            const auto page = service.listInboxPage(dto.offset, dto.limit, dto.origin);
            Json::Value result;
            result["items"] = Json::Value(Json::arrayValue);
            for (const auto& media : page.items) result["items"].append(mediaJson(media));
            result["total"] = Json::Int64(page.total);
            if (page.nextOffset) result["nextOffset"] = Json::Int64(*page.nextOffset);
            else result["nextOffset"] = Json::nullValue;
            return result;
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/inbox/{1}/parse", [&service](const Request& request, Callback&& callback, std::string id) {
        respond(request, std::move(callback), [&] {
            const auto body = request->getJsonObject();
            if (!body || !(*body)["title"].isString() || !(*body)["season"].isString() ||
                !(*body)["episodeNumber"].isString() || !(*body)["episodeType"].isString())
                throw ApiError(400, "invalid_request", "correction fields are required");
            if (body->isMember("bangumiSubjectId"))
                throw ApiError(400, "binding_requires_confirmation", "use confirmed Bangumi binding");
            MediaCorrection correction{};
            correction.title = (*body)["title"].asString();
            correction.season = (*body)["season"].asString();
            correction.episodeNumber = (*body)["episodeNumber"].asString();
            correction.episodeType = (*body)["episodeType"].asString();
            correction.confidence = 1.0;
            return mediaJson(service.correct(parseId(id), correction));
        });
    }, {drogon::Post});
    drogon::app().registerHandler("/api/media/{1}/episode", [&service](const Request& request, Callback&& callback, std::string id) {
        respond(request, std::move(callback), [&] {
            const auto body = request->body().size() <= 1024 ? request->getJsonObject() : nullptr;
            if (!request->getParameters().empty() || !body || !body->isObject() || body->size() != 2 ||
                !(*body)["episodeNumber"].isString() || !(*body)["episodeType"].isString())
                throw ApiError(400, "invalid_episode_edit", "episodeNumber and episodeType required");
            const MediaEpisodeEdit edit{(*body)["episodeNumber"].asString(), (*body)["episodeType"].asString()};
            return mediaJson(service.editEpisode(parseId(id), edit));
        });
    }, {drogon::Put});

}

}
