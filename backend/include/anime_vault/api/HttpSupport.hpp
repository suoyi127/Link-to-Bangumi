#pragma once
#include "anime_vault/api/ApiError.hpp"
#include "anime_vault/domain/MediaError.hpp"
#include "anime_vault/services/OrganizationService.hpp"
#include <drogon/drogon.h>
#include <optional>
#include <set>
#include <string_view>

namespace anime_vault::api {
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
using Request = drogon::HttpRequestPtr;
enum class RequestIdPolicy { None, Media, Novel, Game, QbConfig, BangumiConfig };
struct RequestContext {
    std::string requestId;
    std::size_t responseLimit{};
};
RequestContext requestContext(const Request& request, RequestIdPolicy policy);
std::string requestIdFor(const Request& request);
Json::Value errorJson(const std::string& code, const std::string& message);
void deliver(Callback callback, const RequestContext& context, int status, Json::Value payload);
void deliver(Callback callback, const std::string& requestId, int status, Json::Value payload);
void respond(const Request& request, Callback callback,
             const std::function<Json::Value()>& action, int success = 200);
bool hasOnlyFields(const Json::Value& body, const std::set<std::string>& fields);
bool hasExactFields(const Json::Value& body, const std::set<std::string>& fields);
std::optional<std::string> readStringField(const Json::Value& body, const char* key);
std::optional<std::int64_t> readPositiveIdField(const Json::Value& body, const char* key);
std::int64_t parseId(std::string_view value);
std::int64_t pageNumber(const Request& request, const std::string& name,
                        std::int64_t fallback, std::int64_t minimum, std::int64_t maximum);
int bangumiErrorStatus(const std::string& code);
int novelErrorStatus(const std::string& code);
int gameErrorStatus(const std::string& code);
int qbConfigErrorStatus(const std::string& code);
int bangumiConfigErrorStatus(const std::string& code);
int mediaErrorStatus(MediaFailureKind kind);
ApiError organizationApiError(const OrganizationServiceError& error);
}
