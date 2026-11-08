#pragma once
#include "anime_vault/services/SteamImportService.hpp"
#include "anime_vault/services/SteamLoginService.hpp"
namespace anime_vault::api {
void registerSteamEndpoints(std::shared_ptr<SteamConfiguration> config, std::shared_ptr<SteamImportService> imports,
    std::shared_ptr<SteamLoginService> login = {});
}
