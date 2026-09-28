#pragma once

#include "anime_vault/infrastructure/network/QbConnectionManager.hpp"

#include <json/json.h>
#include <memory>

namespace anime_vault::api {
struct QbConfigRequest { std::string url, username, password; };
QbConfigRequest parseQbConfigRequest(const Json::Value& body);
Json::Value qbConfigJson(const QbConfigSummary& summary);
void registerQbConfigEndpoints(std::shared_ptr<QbConnectionManager> manager);
}
