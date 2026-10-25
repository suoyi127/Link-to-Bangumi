#pragma once
#include "anime_vault/repositories/MediaRepository.hpp"
#include <json/json.h>
#include <filesystem>
#include <functional>

namespace anime_vault::api {
struct EffectiveSettings {
    std::filesystem::path sourcePath, importPath, libraryPath, dataPath;
    bool bangumiConfigured{};
    bool qbConfigured{};
    bool qbDownloadConfigured{};
    bool qbDownloadEnvironmentOverride{};
    std::function<bool()> qbConfiguredNow;
    std::function<bool()> bangumiConfiguredNow;
};
struct QbDownloadDirectoryRequest { std::string path; };
QbDownloadDirectoryRequest parseQbDownloadDirectoryRequest(const Json::Value& body);
UiPreferences parseUiPreferencesRequest(const Json::Value& body);
void registerSettingsController(MediaRepository& repository, EffectiveSettings settings);
}
