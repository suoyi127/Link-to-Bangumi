#pragma once
#include "anime_vault/api/MediaService.hpp"
#include "anime_vault/services/OrganizationService.hpp"
#include <json/json.h>

namespace anime_vault::api {

struct ExecuteOrganizationHttpRequest {
    std::int64_t planId{};
    std::string idempotencyKey;
    bool confirmed{};
    bool qbDownloadComplete{};
};
ExecuteOrganizationHttpRequest parseExecuteOrganizationRequest(const Json::Value& body);
Json::Value executeOrganizationJson(const ExecuteOrganizationResult& result);
void registerOrganizationController(MediaService& service, OrganizationService& organization);
}
