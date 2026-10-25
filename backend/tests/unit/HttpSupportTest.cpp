#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/api/MediaService.hpp"
#include <catch2/catch_test_macros.hpp>
#include <drogon/drogon.h>

using namespace anime_vault::api;
TEST_CASE("HTTP contexts preserve family request ID policies") {
    auto request = drogon::HttpRequest::newHttpRequest();
    request->addHeader("X-Request-Id", "caller");
    REQUIRE(requestContext(request, RequestIdPolicy::Media).requestId == "caller");
    REQUIRE(requestContext(request, RequestIdPolicy::Novel).requestId.starts_with("novel-"));
    REQUIRE(requestContext(request, RequestIdPolicy::Game).requestId.starts_with("game-"));
    REQUIRE(requestContext(request, RequestIdPolicy::QbConfig).requestId == "1");
    REQUIRE(requestContext(request, RequestIdPolicy::BangumiConfig).requestId == "1");
    REQUIRE(requestContext(request, RequestIdPolicy::None).requestId.empty());
    auto plain = drogon::HttpRequest::newHttpRequest();
    auto first = std::stoull(requestContext(plain, RequestIdPolicy::Media).requestId);
    REQUIRE(std::stoull(requestContext(plain, RequestIdPolicy::Media).requestId) == first + 1);
}
TEST_CASE("HTTP JSON replies apply only the selected response policy") {
    drogon::HttpResponsePtr response;
    const auto done = [&](const drogon::HttpResponsePtr& result) { response = result; };
    Json::Value payload; payload["text"] = std::string(1024 * 1024, 'x');
    deliver(done, RequestContext{"media", 1024 * 1024}, 201, payload);
    REQUIRE(response->statusCode() == drogon::k500InternalServerError);
    REQUIRE((*response->getJsonObject())["error"]["code"] == "response_too_large");
    REQUIRE((*response->getJsonObject())["requestId"] == "media");
    REQUIRE(response->getHeader("X-Request-Id") == "media");
    deliver(done, RequestContext{}, 201, payload);
    REQUIRE(response->statusCode() == drogon::k201Created);
    REQUIRE_FALSE(response->getJsonObject()->isMember("requestId"));
    REQUIRE(response->getHeader("X-Request-Id").empty());
    Json::Value compactPayload; compactPayload["ok"] = true;
    deliver(done, RequestContext{std::string(100, 'i'), compactPayload.toStyledString().size()}, 200, compactPayload);
    REQUIRE((*response->getJsonObject())["ok"].asBool());
    REQUIRE(errorJson("code", "message")["error"]["message"] == "message");
}
TEST_CASE("HTTP action fallback hides unexpected exception details") {
    drogon::HttpResponsePtr response;
    auto request = drogon::HttpRequest::newHttpRequest();
    respond(request, [&](const drogon::HttpResponsePtr& result) { response = result; }, []() -> Json::Value {
        throw std::runtime_error("private path");
    });
    REQUIRE(response->statusCode() == drogon::k500InternalServerError);
    REQUIRE((*response->getJsonObject())["error"]["message"] == "internal error");
    respond(request, [&](const drogon::HttpResponsePtr& result) { response = result; }, []() -> Json::Value {
        throw ApiError(409, "conflict", "public");
    });
    REQUIRE(response->statusCode() == drogon::k409Conflict);
    REQUIRE((*response->getJsonObject())["error"]["code"] == "conflict");
}
TEST_CASE("HTTP field checks distinguish allowed exact optional and typed values") {
    Json::Value body(Json::objectValue);
    REQUIRE(hasOnlyFields(body, {"name"}));
    REQUIRE_FALSE(hasExactFields(body, {"name"}));
    REQUIRE_FALSE(readStringField(body, "name"));
    body["name"] = "";
    REQUIRE(hasExactFields(body, {"name"}));
    REQUIRE(readStringField(body, "name") == std::optional<std::string>{""});
    body["id"] = Json::Int64(1);
    REQUIRE(readPositiveIdField(body, "id") == 1);
    REQUIRE_FALSE(hasOnlyFields(body, {"name"}));
    body["id"] = 0; REQUIRE_FALSE(readPositiveIdField(body, "id"));
    body["id"] = "1"; REQUIRE_FALSE(readPositiveIdField(body, "id"));
    body["name"] = Json::nullValue; REQUIRE_FALSE(readStringField(body, "name"));
    REQUIRE_FALSE(hasOnlyFields(Json::Value(Json::arrayValue), {}));
}
TEST_CASE("HTTP Media IDs pagination and protocol mappings retain contracts") {
    REQUIRE(parseId("001") == 1);
    for (const auto* text : {"", "0", "-1", "+1", "1x", "9223372036854775808"})
        REQUIRE_THROWS_AS(parseId(text), ApiError);
    auto request = drogon::HttpRequest::newHttpRequest();
    REQUIRE(pageNumber(request, "limit", 50, 1, 100) == 50);
    request->setParameter("limit", "100"); REQUIRE(pageNumber(request, "limit", 50, 1, 100) == 100);
    request->setParameter("limit", "101"); REQUIRE_THROWS_AS(pageNumber(request, "limit", 50, 1, 100), ApiError);
    REQUIRE(bangumiErrorStatus("bangumi_http_error") == 409);
    REQUIRE(bangumiErrorStatus("bangumi_invalid_query") == 400);
    REQUIRE(bangumiErrorStatus("bangumi_remote_rate_limited") == 429);
    REQUIRE(bangumiErrorStatus("unknown") == 503);
    REQUIRE(novelErrorStatus("other_not_found") == 404);
    REQUIRE(novelErrorStatus("novel_has_accessible_files") == 409);
    REQUIRE(gameErrorStatus("game_path_already_imported") == 409);
    REQUIRE(qbConfigErrorStatus("qb_config_storage_unavailable") == 500);
    REQUIRE(bangumiConfigErrorStatus("bangumi_config_storage_unavailable") == 400);
    REQUIRE(organizationApiError(anime_vault::OrganizationServiceError("execute_rate_limited")).status == 429);
    REQUIRE(organizationApiError(anime_vault::OrganizationServiceError("unknown")).code == "internal_error");
}


TEST_CASE("HTTP error status mappings cover family-specific branches") {
    const auto check = [](auto mapping, const std::initializer_list<std::pair<const char*, int>>& cases) {
        for (const auto& [code, expected] : cases) {
            INFO(code);
            REQUIRE(mapping(code) == expected);
        }
    };
    check(novelErrorStatus, {{"novel_storage_error", 500}, {"novel_file_not_found", 404},
        {"cross_site_request_forbidden", 403}, {"request_too_large", 413},
        {"novel_has_accessible_files", 409}, {"invalid_request", 400}});
    check(gameErrorStatus, {{"game_storage_error", 500}, {"game_not_found", 404},
        {"cross_site_request_forbidden", 403}, {"request_too_large", 413},
        {"game_is_accessible", 409}, {"game_path_already_imported", 409}, {"other_not_found", 400}});
    check(qbConfigErrorStatus, {{"request_too_large", 413}, {"qb_config_storage_failed", 500},
        {"qb_config_corrupt", 500}, {"qb_config_storage_unavailable", 500}, {"invalid_request", 400}});
    check(bangumiConfigErrorStatus, {{"request_too_large", 413}, {"bangumi_config_storage_failed", 500},
        {"bangumi_config_corrupt", 500}, {"invalid_request", 400}});
    check(bangumiErrorStatus, {{"bangumi_invalid_subject_id", 400}, {"bangumi_rate_limited", 429},
        {"bangumi_local_rate_limited", 429}, {"bangumi_queue_full", 503}, {"bangumi_not_animation", 409}});
    for (const auto* code : {"confirmation_required", "qb_completion_required", "invalid_plan_id", "invalid_idempotency_key"}) {
        const auto error = organizationApiError(anime_vault::OrganizationServiceError(code));
        REQUIRE(error.status == 400);
        REQUIRE(error.code == code);
        REQUIRE(std::string(error.what()) == "invalid execution request");
    }
    for (const auto* code : {"plan_not_found", "media_not_found"})
        REQUIRE(organizationApiError(anime_vault::OrganizationServiceError(code)).status == 404);
    for (const auto* code : {"idempotency_key_reused", "execution_in_progress", "plan_already_claimed",
        "plan_expired", "plan_conflict", "source_changed", "invalid_origin", "unsupported_operation",
        "invalid_plan", "invalid_root", "target_exists", "publish_failed", "symlink_unavailable"})
        REQUIRE(organizationApiError(anime_vault::OrganizationServiceError(code)).status == 409);
}

TEST_CASE("HTTP media failure mapping preserves contextual folder import statuses") {
    auto request = drogon::HttpRequest::newHttpRequest();
    for (const auto& [kind, status] : {
        std::pair{anime_vault::MediaFailureKind::missingResource, drogon::k404NotFound},
        std::pair{anime_vault::MediaFailureKind::conflict, drogon::k409Conflict}}) {
        drogon::HttpResponsePtr response;
        respond(request, [&](const drogon::HttpResponsePtr& result) { response = result; }, [kind]() -> Json::Value {
            throw anime_vault::MediaError(kind, "folder_import_not_found", "folder import is unavailable");
        });
        REQUIRE(response->statusCode() == status);
        REQUIRE((*response->getJsonObject())["error"]["code"] == "folder_import_not_found");
        REQUIRE((*response->getJsonObject())["error"]["message"] == "folder import is unavailable");
    }
}
