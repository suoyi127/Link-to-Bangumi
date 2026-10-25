#include "anime_vault/api/HttpSupport.hpp"
#include <atomic>
#include <charconv>

namespace anime_vault::api {
namespace {
std::atomic_uint64_t mediaSequence{}, novelSequence{}, gameSequence{}, qbSequence{}, bangumiSequence{};
}
RequestContext requestContext(const Request& request, RequestIdPolicy policy) {
    switch (policy) {
    case RequestIdPolicy::Media:
        return {request && !request->getHeader("X-Request-Id").empty()
            ? request->getHeader("X-Request-Id") : std::to_string(++mediaSequence), 1024 * 1024};
    case RequestIdPolicy::Novel: return {"novel-" + std::to_string(++novelSequence), 0};
    case RequestIdPolicy::Game: return {"game-" + std::to_string(++gameSequence), 0};
    case RequestIdPolicy::QbConfig: return {std::to_string(++qbSequence), 0};
    case RequestIdPolicy::BangumiConfig: return {std::to_string(++bangumiSequence), 0};
    case RequestIdPolicy::None: return {};
    }
    return {};
}
std::string requestIdFor(const Request& request) {
    return requestContext(request, RequestIdPolicy::Media).requestId;
}
Json::Value errorJson(const std::string& code, const std::string& message) {
    Json::Value payload;
    payload["error"]["code"] = code;
    payload["error"]["message"] = message;
    return payload;
}
void deliver(Callback callback, const RequestContext& context, int status, Json::Value payload) {
    // 保留 Media 在附加请求 ID 之前按 styled JSON 限制大小的约定。
    if (context.responseLimit && payload.toStyledString().size() > context.responseLimit) {
        status = 500;
        payload = errorJson("response_too_large", "response too large");
    }
    if (!context.requestId.empty()) payload["requestId"] = context.requestId;
    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
    if (!context.requestId.empty()) response->addHeader("X-Request-Id", context.requestId);
    callback(response);
}
void deliver(Callback callback, const std::string& requestId, int status, Json::Value payload) {
    deliver(std::move(callback), RequestContext{requestId, 1024 * 1024}, status, std::move(payload));
}
void respond(const Request& request, Callback callback,
             const std::function<Json::Value()>& action, int success) {
    const auto context = requestContext(request, RequestIdPolicy::Media);
    int status = success;
    Json::Value payload;
    try { payload = action(); }
    catch (const ApiError& error) {
        status = error.status;
        payload = errorJson(error.code, error.what());
    } catch (const MediaError& error) {
        status = mediaErrorStatus(error.kind);
        payload = errorJson(error.code, error.what());
    } catch (const std::exception&) {
        status = 500;
        payload = errorJson("internal_error", "internal error");
    }
    deliver(std::move(callback), context, status, std::move(payload));
}
bool hasOnlyFields(const Json::Value& body, const std::set<std::string>& fields) {
    if (!body.isObject()) return false;
    for (const auto& name : body.getMemberNames()) if (!fields.contains(name)) return false;
    return true;
}
bool hasExactFields(const Json::Value& body, const std::set<std::string>& fields) {
    return hasOnlyFields(body, fields) && body.size() == fields.size();
}
std::optional<std::string> readStringField(const Json::Value& body, const char* key) {
    if (!body.isObject() || !body[key].isString()) return std::nullopt;
    return body[key].asString();
}
std::optional<std::int64_t> readPositiveIdField(const Json::Value& body, const char* key) {
    if (!body.isObject() || !body[key].isInt64() || body[key].asInt64() <= 0) return std::nullopt;
    return body[key].asInt64();
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
    if (error != std::errc{} || end != text.data() + text.size() || value < minimum || value > maximum)
        throw ApiError(400, "invalid_page", "invalid page parameter");
    return value;
}
int novelErrorStatus(const std::string& code) {
    return code == "novel_storage_error" ? 500 : code.find("not_found") != std::string::npos ? 404 :
        code == "cross_site_request_forbidden" ? 403 : code == "request_too_large" ? 413 : code == "novel_has_accessible_files" ? 409 : 400;
}
int gameErrorStatus(const std::string& code) {
    return code == "game_storage_error" ? 500 : code == "game_not_found" ? 404 : code == "cross_site_request_forbidden" ? 403 : code == "request_too_large" ? 413 : code == "game_is_accessible" || code == "game_path_already_imported" ? 409 : 400;
}
int qbConfigErrorStatus(const std::string& code) {
    return code == "request_too_large" ? 413 : code == "qb_config_storage_failed" ||
        code == "qb_config_corrupt" || code == "qb_config_storage_unavailable" ? 500 : 400;
}
int bangumiConfigErrorStatus(const std::string& code) {
    return code == "request_too_large" ? 413 : code == "bangumi_config_storage_failed" || code == "bangumi_config_corrupt" ? 500 : 400;
}
int bangumiErrorStatus(const std::string& code) {
    if (code == "bangumi_invalid_query" || code == "bangumi_invalid_subject_id") return 400;
    if (code == "bangumi_rate_limited" || code == "bangumi_local_rate_limited" || code == "bangumi_remote_rate_limited") return 429;
    if (code == "bangumi_queue_full") return 503;
    if (code == "bangumi_not_animation" || code == "bangumi_http_error") return 409;
    return 503;
}
int mediaErrorStatus(MediaFailureKind kind) {
    switch (kind) {
    case MediaFailureKind::invalidInput: return 400;
    case MediaFailureKind::missingResource: return 404;
    case MediaFailureKind::conflict: return 409;
    }
    return 500;
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
}
