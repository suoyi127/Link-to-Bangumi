#pragma once
#include "anime_vault/services/NovelBangumiService.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
namespace anime_vault::api {
void registerNovelEndpoints(std::shared_ptr<NovelService> novels,
                            std::shared_ptr<NovelBangumiService> bangumi,
                            ProcessLauncher& launcher);
}
