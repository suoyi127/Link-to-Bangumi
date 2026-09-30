#pragma once
#include "anime_vault/services/GameBangumiService.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
namespace anime_vault::api {
void registerGameEndpoints(std::shared_ptr<GameService> games, std::shared_ptr<GameBangumiService> bangumi, ProcessLauncher& launcher);
}
