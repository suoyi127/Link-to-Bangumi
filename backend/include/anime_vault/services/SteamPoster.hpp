#pragma once
#include "anime_vault/domain/Game.hpp"
#include "anime_vault/domain/CachedCover.hpp"
namespace anime_vault {
bool usesSteamPoster(const GameResource& game);
std::optional<CachedCover> readSteamPoster(const GameResource& game);
}
