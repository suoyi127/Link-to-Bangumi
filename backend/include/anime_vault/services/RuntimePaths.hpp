#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace anime_vault {

struct RuntimePaths {
    std::filesystem::path data;
    std::filesystem::path source;
    std::filesystem::path imported;
    std::filesystem::path library;
    bool qbConfigured{};
};

std::filesystem::path defaultDataDirectory();
std::filesystem::path defaultMediaDirectory();
RuntimePaths resolveRuntimePaths(std::filesystem::path data,
    std::filesystem::path imported, std::filesystem::path library,
    std::optional<std::filesystem::path> qbOverride, std::string_view storedQb);

} // namespace anime_vault
