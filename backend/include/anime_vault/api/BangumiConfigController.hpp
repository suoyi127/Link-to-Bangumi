#pragma once

#include "anime_vault/infrastructure/network/BangumiConnectionManager.hpp"
#include <json/json.h>
#include <memory>

namespace anime_vault::api {
std::string parseBangumiUserAgent(const Json::Value& body);
void registerBangumiConfigEndpoints(std::shared_ptr<BangumiConnectionManager> manager);
}
