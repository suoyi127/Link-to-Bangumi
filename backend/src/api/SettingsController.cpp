#include "anime_vault/api/SettingsController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/services/RuntimePaths.hpp"
#include <algorithm>
#include <filesystem>

namespace anime_vault::api {
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

void registerSettingsController(MediaRepository& repository, EffectiveSettings settings) {
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
            const auto saved = repository.getUiPreferences();
            preferences.qbDownloadDirectory = saved.qbDownloadDirectory;
            preferences.playerType = saved.playerType;
            preferences.playerExecutable = saved.playerExecutable;
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

}
