#include "anime_vault/api/MediaController.hpp"
#include "anime_vault/api/MediaService.hpp"
#include "anime_vault/services/RuntimePaths.hpp"

#include <drogon/drogon.h>
#include <atomic>
#include <charconv>
#include <algorithm>
#include <memory>
#include <filesystem>

namespace anime_vault::api {
namespace {
std::atomic_uint64_t requestSequence{0};
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
using Request = drogon::HttpRequestPtr;

std::string requestIdFor(const Request& request) {
    return request->getHeader("X-Request-Id").empty()
        ? std::to_string(++requestSequence) : request->getHeader("X-Request-Id");
}
void deliver(Callback callback, const std::string& requestId, int status, Json::Value payload) {
    if (payload.toStyledString().size() > 1024 * 1024) {
        status = 500;
        payload = Json::Value();
        payload["error"]["code"] = "response_too_large";
        payload["error"]["message"] = "response too large";
    }
    payload["requestId"] = requestId;
    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
    response->addHeader("X-Request-Id", requestId);
    callback(response);
}
Json::Value errorJson(const std::string& code, const std::string& message) {
    Json::Value payload;
    payload["error"]["code"] = code;
    payload["error"]["message"] = message;
    return payload;
}

std::int64_t parseId(std::string_view value) {
    std::int64_t id{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), id);
    if (error != std::errc{} || end != value.data() + value.size() || id <= 0)
        throw ApiError(400, "invalid_id", "id must be positive");
    return id;
}
std::int64_t pageNumber(const Request& request, const std::string& name,
                        std::int64_t fallback, std::int64_t minimum, std::int64_t maximum) {
    const auto found = request->getParameters().find(name);
    if (found == request->getParameters().end()) return fallback;
    const auto& text = found->second;
    std::int64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value < minimum || value > maximum)
        throw ApiError(400, "invalid_page", "invalid page parameter");
    return value;
}
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
        if (*dto.origin != "qb_download" && *dto.origin != "external_import")
            throw ApiError(400, "invalid_origin", "invalid inbox origin");
    }
    return dto;
}
Json::Value scanJson(const ScanRecord& scan) {
    Json::Value json;
    json["id"] = Json::Int64(scan.id);
    json["source"] = scan.source;
    json["status"] = scan.status;
    json["discoveredCount"] = Json::Int64(scan.discoveredCount);
    json["processedCount"] = Json::Int64(scan.processedCount);
    json["errorCount"] = Json::Int64(scan.errorCount);
    // Scanner exceptions may contain private paths; never return their raw text.
    json["errorSummary"] = scan.errorSummary.empty() ? "" : "scan_failed";
    return json;
}
Json::Value mediaJson(const MediaRecord& media) {
    Json::Value json;
    json["id"] = Json::Int64(media.id);
    json["scanId"] = Json::Int64(media.scanId);
    json["sourcePath"] = media.sourcePath;
    json["filename"] = media.filename;
    json["parsedTitle"] = media.parsedTitle;
    json["title"] = media.title;
    json["season"] = media.season;
    json["episodeNumber"] = media.episodeNumber;
    json["episodeType"] = media.episodeType;
    json["sizeBytes"] = Json::Int64(media.sizeBytes);
    json["status"] = media.status;
    json["origin"] = media.origin;
    if (media.animeId) json["animeId"] = Json::Int64(*media.animeId);
    else json["animeId"] = Json::nullValue;
    json["confidence"] = media.confidence;
    if (media.bangumiSubjectId) json["bangumiSubjectId"] = Json::Int64(*media.bangumiSubjectId);
    return json;
}
Json::Value animeJson(const AnimeRecord& anime, bool detail) {
    Json::Value json;
    json["id"] = Json::Int64(anime.id);
    json["displayTitle"] = anime.displayTitle;
    json["originalTitle"] = anime.originalTitle;
    json["season"] = anime.season;
    if (anime.year) json["year"] = *anime.year;
    if (anime.bangumiSubjectId) json["bangumiSubjectId"] = Json::Int64(*anime.bangumiSubjectId);
    json["coverUrl"] = anime.coverUrl;
    json["locked"] = anime.locked;
    if (detail) {
        json["aliases"] = Json::Value(Json::arrayValue);
        for (const auto& alias : anime.aliases) json["aliases"].append(alias);
        json["media"] = Json::Value(Json::arrayValue);
        for (const auto& media : anime.media) json["media"].append(mediaJson(media));
        if (anime.nextMediaOffset) json["nextMediaOffset"] = Json::Int64(*anime.nextMediaOffset);
        else json["nextMediaOffset"] = Json::nullValue;
    }
    return json;
}
Json::Value candidateJson(const BangumiCandidate& candidate) {
    Json::Value json;
    json["id"] = Json::Int64(candidate.id);
    json["name"] = candidate.name;
    json["nameCn"] = candidate.nameCn;
    json["date"] = candidate.date;
    json["coverUrl"] = candidate.coverUrl;
    json["episodeCount"] = candidate.episodeCount;
    json["type"] = candidate.type;
    json["score"] = candidate.score;
    return json;
}
int bangumiErrorStatus(const std::string& code) {
    if (code == "bangumi_invalid_query" || code == "bangumi_invalid_subject_id") return 400;
    if (code == "bangumi_rate_limited") return 429;
    if (code == "bangumi_not_animation" || code == "bangumi_http_error") return 409;
    return 503;
}
Json::Value previewJson(const PreviewResponse& preview) {
    Json::Value json;
    json["id"] = Json::Int64(preview.id);
    json["targetPath"] = preview.targetPath;
    json["operation"] = preview.operation;
    json["expiresAt"] = preview.expiresAt;
    Json::Value conflicts(Json::arrayValue);
    for (const auto& conflict : preview.conflicts) conflicts.append(conflict);
    json["conflicts"] = conflicts;
    return json;
}
ApiError organizationApiError(const OrganizationServiceError& error) {
    const auto& code = error.code();
    if (code == "execute_rate_limited")
        return {429, code, "wait before retrying execution"};
    if (code == "confirmation_required" || code == "qb_completion_required" ||
        code == "invalid_plan_id" || code == "invalid_idempotency_key")
        return {400, code, "invalid execution request"};
    if (code == "plan_not_found" || code == "media_not_found")
        return {404, code, "organization plan not found"};
    if (code == "idempotency_key_reused" || code == "execution_in_progress" ||
        code == "plan_already_claimed" || code == "plan_expired" ||
        code == "plan_conflict" || code == "source_changed" ||
        code == "invalid_origin" || code == "unsupported_operation" ||
        code == "invalid_plan" || code == "invalid_root" ||
        code == "target_exists" || code == "publish_failed" ||
        code == "symlink_unavailable")
        return {409, code, "organization cannot be completed"};
    return {500, "internal_error", "internal error"};
}
void respond(const Request& request, Callback callback,
             const std::function<Json::Value()>& action, int success = 200) {
    const auto requestId = requestIdFor(request);
    int status = success;
    Json::Value payload;
    try { payload = action(); }
    catch (const ApiError& error) {
        status = error.status;
        payload["error"]["code"] = error.code;
        payload["error"]["message"] = error.what();
    } catch (const std::exception&) {
        status = 500;
        payload["error"]["code"] = "internal_error";
        payload["error"]["message"] = "internal error";
    }
    deliver(std::move(callback), requestId, status, std::move(payload));
}
} // namespace

PlayMediaHttpRequest parsePlayMediaRequest(std::string_view rawId) {
    return {parseId(rawId)};
}

bool playbackOriginAllowed(std::string_view origin) {
    return origin == "http://127.0.0.1:5173" || origin == "http://localhost:5173" ||
        origin == "http://127.0.0.1:8848" || origin == "http://localhost:8848";
}

void registerPlaybackEndpoint(MediaRepository& repository, PlaybackService& playback) {
    drogon::app().registerHandler("/api/media/{1}/play", [&repository, &playback](
        const Request& request, Callback&& callback, std::string rawId) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "playback takes no body or query");
            const auto origin = request->getHeader("Origin");
            if ((!origin.empty() && !playbackOriginAllowed(origin)) ||
                request->getHeader("Sec-Fetch-Site") == "cross-site")
                throw ApiError(403, "playback_origin_forbidden", "playback requires a local page");
            const auto dto = parsePlayMediaRequest(rawId);
            try {
                playback.play(dto.mediaId, repository.getUiPreferences().mpvExecutable);
            } catch (const PlaybackError& error) {
                const auto& code = error.code();
                const int status = code == "invalid_media_id" ? 400 :
                    code == "media_not_found" || code == "playback_file_missing" ? 404 :
                    code == "mpv_launch_failed" ? 503 : 409;
                throw ApiError(status, code, "playback could not start");
            }
            Json::Value result;
            result["mediaId"] = Json::Int64(dto.mediaId);
            result["status"] = "started";
            return result;
        }, 202);
    }, {drogon::Post});
}

UiPreferences parseUiPreferencesRequest(const Json::Value& body) {
    if (!body.isObject() || body.size() != 4)
        throw ApiError(400, "invalid_request", "four preference fields are required");
    for (const auto& name : body.getMemberNames())
        if (name != "preferredOperation" && name != "scanIntervalSeconds" &&
            name != "mpvExecutable" && name != "qbWebUiUrl")
            throw ApiError(400, "invalid_request", "unknown setting field");
    if (!body["preferredOperation"].isString() || !body["scanIntervalSeconds"].isInt() ||
        !body["mpvExecutable"].isString() || !body["qbWebUiUrl"].isString())
        throw ApiError(400, "invalid_settings", "invalid preference type");
    UiPreferences result{body["preferredOperation"].asString(), body["scanIntervalSeconds"].asInt(),
                         body["mpvExecutable"].asString(), body["qbWebUiUrl"].asString()};
    if (result.preferredOperation != "hardlink" && result.preferredOperation != "copy" &&
        result.preferredOperation != "symlink")
        throw ApiError(400, "invalid_settings", "invalid preferred operation");
    if (result.scanIntervalSeconds < 60 || result.scanIntervalSeconds > 86400 ||
        result.mpvExecutable.size() > 1024 || result.qbWebUiUrl.size() > 2048)
        throw ApiError(400, "invalid_settings", "preference out of range");
    const auto printable = [](const std::string& value) {
        return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch >= 0x20 && ch != 0x7f;
        });
    };
    if (!printable(result.mpvExecutable) || !printable(result.qbWebUiUrl))
        throw ApiError(400, "invalid_settings", "control characters are not allowed");
    if (!result.qbWebUiUrl.empty()) {
        const auto& url = result.qbWebUiUrl;
        const auto schemeEnd = url.find("://");
        if (schemeEnd == std::string::npos ||
            (url.substr(0, schemeEnd) != "http" && url.substr(0, schemeEnd) != "https"))
            throw ApiError(400, "invalid_settings", "invalid qB URL");
        const auto authorityStart = schemeEnd + 3;
        const auto authorityEnd = url.find_first_of("/?#", authorityStart);
        const auto authority = url.substr(authorityStart, authorityEnd - authorityStart);
        const auto suffix = authorityEnd == std::string::npos ? std::string{} : url.substr(authorityEnd);
        const bool loopback = authority == "localhost" || authority == "127.0.0.1" ||
            authority == "[::1]" || authority.rfind("localhost:", 0) == 0 ||
            authority.rfind("127.0.0.1:", 0) == 0 || authority.rfind("[::1]:", 0) == 0;
        const auto colon = authority.rfind(':');
        bool portValid = true;
        if (colon != std::string::npos && authority != "[::1]") {
            const auto portText = std::string_view(authority).substr(colon + 1);
            unsigned int port{};
            const auto [end, error] = std::from_chars(
                portText.data(), portText.data() + portText.size(), port);
            portValid = error == std::errc{} &&
                end == portText.data() + portText.size() && port >= 1 && port <= 65535;
        }
        if (!loopback || !portValid || authority.find('@') != std::string::npos ||
            suffix.find('?') != std::string::npos || suffix.find('#') != std::string::npos)
            throw ApiError(400, "invalid_settings", "qB URL must be loopback without credentials or query");
    }
    return result;
}

QbDownloadDirectoryRequest parseQbDownloadDirectoryRequest(const Json::Value& body) {
    if (!body.isObject() || body.size() != 1 || !body["path"].isString())
        throw ApiError(400, "invalid_request", "path string is required");
    const auto path = body["path"].asString();
    if (path.size() > 4096 || path.find('\0') != std::string::npos)
        throw ApiError(400, "invalid_qb_download_directory", "invalid qB directory path");
    return {path};
}

void registerManagementEndpoints(MediaRepository& repository, EffectiveSettings settings) {
    const auto pathString = [](const std::filesystem::path& path) {
        const auto bytes = std::filesystem::absolute(path).lexically_normal().u8string();
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    auto settingsJson = [&repository, settings, pathString] {
        const auto preferences = repository.getUiPreferences();
        Json::Value json;
        json["sourcePath"] = pathString(settings.sourcePath);
        json["qbDownloadDirectory"] = preferences.qbDownloadDirectory;
        json["qbDownloadConfigured"] = settings.qbDownloadConfigured;
        json["qbDownloadEnvironmentOverride"] = settings.qbDownloadEnvironmentOverride;
        json["importPath"] = pathString(settings.importPath);
        json["libraryPath"] = pathString(settings.libraryPath);
        json["dataPath"] = pathString(settings.dataPath);
        json["bangumiConfigured"] = settings.bangumiConfiguredNow
            ? settings.bangumiConfiguredNow() : settings.bangumiConfigured;
        json["qbWebUiConfigured"] = settings.qbConfiguredNow
            ? settings.qbConfiguredNow() : settings.qbConfigured;
        json["preferredOperation"] = preferences.preferredOperation;
        json["scanIntervalSeconds"] = preferences.scanIntervalSeconds;
        json["mpvExecutable"] = preferences.mpvExecutable;
        json["qbWebUiUrl"] = preferences.qbWebUiUrl;
        return json;
    };
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
    drogon::app().registerHandler("/api/audit-logs", [&repository](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty()) throw ApiError(400, "invalid_request", "body not allowed");
            for (const auto& [name, _] : request->getParameters())
                if (name != "limit" && name != "offset" && name != "animeId")
                    throw ApiError(400, "invalid_page", "unknown page parameter");
            const auto limit = static_cast<int>(pageNumber(request, "limit", 50, 1, 100));
            const auto offset = pageNumber(request, "offset", 0, 0, 1'000'000);
            std::optional<std::int64_t> animeId;
            if (request->getParameters().contains("animeId"))
                animeId = pageNumber(request, "animeId", 0, 1, INT64_MAX);
            const auto page = repository.listAuditPage(offset, limit, animeId);
            Json::Value result;
            result["items"] = Json::Value(Json::arrayValue);
            for (const auto& item : page.items) {
                Json::Value json;
                json["id"] = Json::Int64(item.id);
                json["action"] = item.action;
                json["entityType"] = item.entityType;
                json["entityId"] = item.entityId;
                json["createdAt"] = item.createdAt;
                result["items"].append(json);
            }
            result["nextOffset"] = page.nextOffset ? Json::Value(Json::Int64(*page.nextOffset)) : Json::Value(Json::nullValue);
            return result;
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/settings", [settingsJson](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "settings read takes no input");
            return settingsJson();
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/settings", [&repository, settingsJson](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (request->body().size() > 4096)
                throw ApiError(400, "request_too_large", "settings request is too large");
            if (!request->getParameters().empty())
                throw ApiError(400, "invalid_request", "settings write takes no query");
            const auto body = request->getJsonObject();
            if (!body) throw ApiError(400, "invalid_request", "JSON object required");
            auto preferences = parseUiPreferencesRequest(*body);
            preferences.qbDownloadDirectory = repository.getUiPreferences().qbDownloadDirectory;
            repository.putUiPreferences(preferences);
            return settingsJson();
        });
    }, {drogon::Put});
    drogon::app().registerHandler("/api/settings/qb-download-directory", [&repository, settings, settingsJson](
        const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (request->body().size() > 8192)
                throw ApiError(400, "request_too_large", "settings request is too large");
            if (!request->getParameters().empty())
                throw ApiError(400, "invalid_request", "directory write takes no query");
            const auto body = request->getJsonObject();
            if (!body) throw ApiError(400, "invalid_request", "JSON object required");
            const auto dto = parseQbDownloadDirectoryRequest(*body);
            if (settings.qbDownloadEnvironmentOverride)
                throw ApiError(409, "qb_download_directory_overridden",
                    "remove ANIME_VAULT_SOURCE_DIR before changing the directory here");
            auto preferences = repository.getUiPreferences();
            if (dto.path.empty()) {
                preferences.qbDownloadDirectory.clear();
            } else {
                try {
                    const auto* bytes = reinterpret_cast<const char8_t*>(dto.path.data());
                    const auto path = std::filesystem::path(std::u8string_view(bytes, dto.path.size()));
                    const auto validated = validateQbDownloadDirectory(path,
                        settings.importPath, settings.libraryPath, settings.dataPath);
                    const auto encoded = validated.u8string();
                    preferences.qbDownloadDirectory.assign(
                        reinterpret_cast<const char*>(encoded.data()), encoded.size());
                } catch (const RuntimePathError& error) {
                    throw ApiError(error.code == "overlapping_roots" ? 409 : 400,
                        error.code, error.what());
                } catch (const std::exception&) {
                    throw ApiError(400, "invalid_qb_download_directory", "invalid qB directory path");
                }
            }
            repository.putUiPreferences(preferences);
            auto result = settingsJson();
            result["restartRequired"] = true;
            return result;
        });
    }, {drogon::Put});
}

ExecuteOrganizationHttpRequest parseExecuteOrganizationRequest(const Json::Value& body) {
    if (!body.isObject()) throw ApiError(400, "invalid_request", "JSON object required");
    for (const auto& name : body.getMemberNames()) {
        if (name != "planId" && name != "idempotencyKey" &&
            name != "confirmed" && name != "qbDownloadComplete")
            throw ApiError(400, "invalid_request", "unknown execution field");
    }
    if (!body["planId"].isInt64() || body["planId"].asInt64() <= 0)
        throw ApiError(400, "invalid_plan_id", "planId must be a positive integer");
    if (!body["idempotencyKey"].isString())
        throw ApiError(400, "invalid_idempotency_key", "invalid idempotencyKey");
    const auto key = body["idempotencyKey"].asString();
    if (key.empty() || key.size() > 128 ||
        !std::all_of(key.begin(), key.end(), [](unsigned char c) { return c >= 0x21 && c <= 0x7e; }))
        throw ApiError(400, "invalid_idempotency_key", "invalid idempotencyKey");
    if (!body["confirmed"].isBool() || !body["confirmed"].asBool())
        throw ApiError(400, "confirmation_required", "explicit confirmation required");
    if (body.isMember("qbDownloadComplete") && !body["qbDownloadComplete"].isBool())
        throw ApiError(400, "invalid_request", "qbDownloadComplete must be boolean");
    return {body["planId"].asInt64(), key, true,
        body.isMember("qbDownloadComplete") && body["qbDownloadComplete"].asBool()};
}

BindAnimeHttpRequest parseBindAnimeRequest(const Json::Value& body) {
    if (!body.isObject()) throw ApiError(400, "invalid_request", "JSON object required");
    for (const auto& name : body.getMemberNames())
        if (name != "subjectId" && name != "confirmed")
            throw ApiError(400, "invalid_request", "unknown binding field");
    if (!body["subjectId"].isInt64() || body["subjectId"].asInt64() <= 0)
        throw ApiError(400, "bangumi_invalid_subject_id", "subjectId must be positive");
    if (!body["confirmed"].isBool() || !body["confirmed"].asBool())
        throw ApiError(400, "confirmation_required", "explicit confirmation required");
    return {body["subjectId"].asInt64(), true};
}

Json::Value executeOrganizationJson(const ExecuteOrganizationResult& result) {
    Json::Value json;
    json["jobId"] = Json::Int64(result.jobId);
    json["targetPath"] = result.targetPath;
    json["bytes"] = Json::Int64(result.bytes);
    json["status"] = result.status;
    return json;
}

void registerMediaEndpoints(MediaService& service, OrganizationService& organization,
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
            if (scan.status == "completed") {
                // qB RSS supplies aliases; Bangumi is queried only after local titles are normalized.
                if (mikanEnricher) mikanEnricher->runOnce([enricher](MikanEnrichmentResult) {
                    if (enricher) enricher->runOnce();
                });
                else if (enricher) enricher->runOnce();
            }
            return scanJson(scan);
        }, 201);
    }, {drogon::Post});
    drogon::app().registerHandler("/api/imports/scan", [&service](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "import scan takes no body or parameters");
            return scanJson(service.createImportScan());
        }, 201);
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
    drogon::app().registerHandler("/api/organize/preview", [&service](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            const auto body = request->getJsonObject();
            if (!body || !(*body)["mediaFileId"].isInt64())
                throw ApiError(400, "invalid_request", "mediaFileId is required");
            if (body->isMember("operation") && !(*body)["operation"].isString())
                throw ApiError(400, "invalid_operation", "operation must be a string");
            const auto operation = body->isMember("operation") ? (*body)["operation"].asString() : "hardlink";
            return previewJson(service.preview((*body)["mediaFileId"].asInt64(), operation));
        }, 201);
    }, {drogon::Post});
    drogon::app().registerHandler("/api/organize/execute", [&organization](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (request->body().size() > 4096)
                throw ApiError(400, "request_too_large", "execution request is too large");
            if (!request->getParameters().empty())
                throw ApiError(400, "invalid_request", "execution takes no query parameters");
            const auto body = request->getJsonObject();
            if (!body) throw ApiError(400, "invalid_request", "JSON object required");
            const auto dto = parseExecuteOrganizationRequest(*body);
            try {
                return executeOrganizationJson(organization.execute(
                    {dto.planId, dto.idempotencyKey, dto.confirmed, dto.qbDownloadComplete}));
            } catch (const OrganizationServiceError& error) {
                throw organizationApiError(error);
            }
        });
    }, {drogon::Post});
}

void registerAnimeEndpoints(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                            std::shared_ptr<CoverScraper> covers) {
    drogon::app().registerHandler("/api/anime", [&repository](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty())
                throw ApiError(400, "invalid_request", "anime list takes no body");
            for (const auto& [name, _] : request->getParameters())
                if (name != "limit" && name != "offset")
                    throw ApiError(400, "invalid_page", "unknown page parameter");
            const auto limit = static_cast<int>(pageNumber(request, "limit", 50, 1, 200));
            const auto offset = pageNumber(request, "offset", 0, 0, 1'000'000);
            const auto page = repository.listAnimePage(offset, limit);
            Json::Value result;
            result["items"] = Json::Value(Json::arrayValue);
            for (const auto& anime : page.items) result["items"].append(animeJson(anime, false));
            if (page.nextOffset) result["nextOffset"] = Json::Int64(*page.nextOffset);
            else result["nextOffset"] = Json::nullValue;
            return result;
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/anime/{1}", [&repository](const Request& request, Callback&& callback, std::string id) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty())
                throw ApiError(400, "invalid_request", "anime detail takes no body");
            for (const auto& [name, _] : request->getParameters())
                if (name != "mediaLimit" && name != "mediaOffset")
                    throw ApiError(400, "invalid_page", "unknown media page parameter");
            const auto limit = static_cast<int>(pageNumber(request, "mediaLimit", 50, 1, 200));
            const auto offset = pageNumber(request, "mediaOffset", 0, 0, 1'000'000);
            const auto anime = repository.getAnime(parseId(id), offset, limit);
            if (!anime) throw ApiError(404, "anime_not_found", "anime not found");
            return animeJson(*anime, true);
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/bangumi/search", [bangumi](const Request& request, Callback&& callback) mutable {
        const auto requestId = requestIdFor(request);
        if (!request->body().empty() || request->getParameters().size() != 1 ||
            request->getParameters().find("q") == request->getParameters().end()) {
            deliver(std::move(callback), requestId, 400, errorJson("invalid_request", "q is required"));
            return;
        }
        const auto query = request->getParameter("q");
        if (query.empty() || query.size() > 500) {
            deliver(std::move(callback), requestId, 400,
                    errorJson("bangumi_invalid_query", "invalid query"));
            return;
        }
        bangumi->search({query}, [requestId, callback = std::move(callback)](BangumiSearchResult result) mutable {
            if (!result.errorCode.empty()) {
                auto payload = errorJson(result.errorCode, "Bangumi search unavailable");
                payload["retryCount"] = result.retryCount;
                if (result.retryAfter)
                    payload["retryAfter"] = Json::Int64(std::chrono::duration_cast<std::chrono::seconds>(
                        result.retryAfter->time_since_epoch()).count());
                deliver(std::move(callback), requestId, bangumiErrorStatus(result.errorCode), std::move(payload));
                return;
            }
            Json::Value payload;
            payload["items"] = Json::Value(Json::arrayValue);
            for (const auto& candidate : result.candidates.items)
                payload["items"].append(candidateJson(candidate));
            payload["autoBindEligible"] = result.candidates.autoBindEligible;
            payload["fromCache"] = result.fromCache;
            if (result.localMatch) {
                Json::Value local;
                local["animeId"] = Json::Int64(result.localMatch->id);
                local["displayTitle"] = result.localMatch->displayTitle;
                if (result.localMatch->bangumiSubjectId)
                    local["bangumiSubjectId"] = Json::Int64(*result.localMatch->bangumiSubjectId);
                payload["localMatch"] = std::move(local);
            } else payload["localMatch"] = Json::nullValue;
            deliver(std::move(callback), requestId, 200, std::move(payload));
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/anime/{1}/bangumi", [&repository, bangumi, covers](
        const Request& request, Callback&& callback, std::string rawId) mutable {
        const auto requestId = requestIdFor(request);
        std::int64_t animeId{};
        BindAnimeHttpRequest dto;
        try {
            animeId = parseId(rawId);
            if (request->body().size() > 4096)
                throw ApiError(400, "request_too_large", "binding request is too large");
            if (!request->getParameters().empty())
                throw ApiError(400, "invalid_request", "binding takes no query parameters");
            const auto body = request->getJsonObject();
            if (!body) throw ApiError(400, "invalid_request", "JSON object required");
            dto = parseBindAnimeRequest(*body);
            if (!repository.getAnime(animeId))
                throw ApiError(404, "anime_not_found", "anime not found");
        } catch (const ApiError& error) {
            deliver(std::move(callback), requestId, error.status, errorJson(error.code, error.what()));
            return;
        } catch (...) {
            deliver(std::move(callback), requestId, 500, errorJson("internal_error", "internal error"));
            return;
        }
        bangumi->subject(dto.subjectId, [&repository, covers, animeId, requestId,
                                         callback = std::move(callback)](BangumiSubjectResult result) mutable {
            if (!result.errorCode.empty()) {
                deliver(std::move(callback), requestId, bangumiErrorStatus(result.errorCode),
                        errorJson(result.errorCode, "Bangumi subject unavailable"));
                return;
            }
            try {
                const auto bound = repository.bindAnime(animeId, *result.subject);
                if (covers && !result.subject->coverUrl.empty())
                    covers->cacheSubject(animeId, *result.subject);
                auto payload = animeJson(bound, true);
                deliver(std::move(callback), requestId, 200, std::move(payload));
            } catch (const AnimeBindingError& error) {
                const int status = error.code() == "anime_not_found" ? 404 :
                    error.code() == "invalid_id" || error.code() == "invalid_subject" ? 400 : 409;
                deliver(std::move(callback), requestId, status,
                        errorJson(error.code(), "anime binding cannot be completed"));
            } catch (...) {
                deliver(std::move(callback), requestId, 500, errorJson("internal_error", "internal error"));
            }
        });
    }, {drogon::Put});
    drogon::app().registerHandler("/api/anime/{1}/cover/refresh", [covers](
        const Request& request, Callback&& callback, std::string rawId) mutable {
        const auto requestId = requestIdFor(request);
        if (!covers) {
            deliver(std::move(callback), requestId, 503,
                    errorJson("cover_unavailable", "cover scraper unavailable"));
            return;
        }
        std::int64_t animeId{};
        try {
            animeId = parseId(rawId);
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "cover refresh takes no input");
        } catch (const ApiError& error) {
            deliver(std::move(callback), requestId, error.status, errorJson(error.code, error.what()));
            return;
        }
        covers->refresh(animeId, [requestId, callback = std::move(callback)](
            CoverScrapeResult result) mutable {
            if (!result.errorCode.empty()) {
                const int status = result.errorCode == "anime_not_found" ? 404 :
                    result.errorCode == "cover_subject_unbound" ? 409 : 503;
                deliver(std::move(callback), requestId, status,
                        errorJson(result.errorCode, "cover refresh unavailable"));
                return;
            }
            Json::Value payload;
            payload["coverUrl"] = result.coverUrl;
            deliver(std::move(callback), requestId, 200, std::move(payload));
        });
    }, {drogon::Post});
    drogon::app().registerHandler("/api/covers/{1}/{2}/{3}", [covers](
        const Request& request, Callback&& callback, std::string rawAnimeId,
        std::string rawSubjectId, std::string filename) mutable {
        std::optional<CachedCover> image;
        try {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "cover read takes no input");
            if (covers) image = covers->readCached(parseId(rawAnimeId), parseId(rawSubjectId), filename);
        } catch (...) { image.reset(); }
        if (!image) {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k200OK);
        response->setContentTypeString(image->mimeType);
        response->addHeader("Cache-Control", "private, max-age=86400");
        response->addHeader("X-Content-Type-Options", "nosniff");
        response->setBody(std::move(image->bytes));
        callback(response);
    }, {drogon::Get});
}
} // namespace anime_vault::api
