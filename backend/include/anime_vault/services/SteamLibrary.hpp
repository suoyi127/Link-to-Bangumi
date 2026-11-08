#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace anime_vault {
struct SteamInstalledGame {
    std::string appId, title, installPath, manifestPath, steamExecutable;
};
struct SteamScanResult {
    std::vector<SteamInstalledGame> games;
    std::vector<std::string> warnings;
};
class SteamLibrary {
public:
    static std::filesystem::path detectRoot();
    static SteamScanResult scan(const std::filesystem::path& root);
    static bool available(const SteamInstalledGame& game);
};
}
