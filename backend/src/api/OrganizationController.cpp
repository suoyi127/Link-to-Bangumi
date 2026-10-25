#include "anime_vault/api/OrganizationController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/api/MediaJson.hpp"
#include <algorithm>

namespace anime_vault::api {
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

Json::Value executeOrganizationJson(const ExecuteOrganizationResult& result) {
    Json::Value json;
    json["jobId"] = Json::Int64(result.jobId);
    json["targetPath"] = result.targetPath;
    json["bytes"] = Json::Int64(result.bytes);
    json["status"] = result.status;
    return json;
}

void registerOrganizationController(MediaService& service, OrganizationService& organization) {
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
}
