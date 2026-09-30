#pragma once
#include "anime_vault/repositories/MediaRepository.hpp"
#include <string>
#include <vector>

namespace anime_vault {
struct PlayerOption { std::string id, name, executable; bool available{}; };
bool validPlayerType(const std::string& type);
std::string normalizePlayerExecutable(std::string path);
bool validPlayerExecutable(const std::string& path);
std::vector<PlayerOption> discoverPlayers(const UiPreferences& preferences);
}
