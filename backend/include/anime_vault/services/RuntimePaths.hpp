#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace anime_vault {

struct RuntimePaths {
    std::filesystem::path data;
    std::filesystem::path source;
    std::filesystem::path imported;
    std::filesystem::path library;
    bool qbConfigured{};
};

class RuntimePathError : public std::invalid_argument {
public:
    RuntimePathError(std::string code, std::string message)
        : std::invalid_argument(std::move(message)), code(std::move(code)) {}
    std::string code;
};

std::filesystem::path defaultDataDirectory();
std::filesystem::path defaultMediaDirectory();
RuntimePaths resolveRuntimePaths(std::filesystem::path data,
    std::filesystem::path imported, std::filesystem::path library,
    std::optional<std::filesystem::path> qbOverride, std::string_view storedQb);
std::filesystem::path validateQbDownloadDirectory(const std::filesystem::path& candidate,
    const std::filesystem::path& imported, const std::filesystem::path& library,
    const std::filesystem::path& data);
bool qbDirectoryActive(const std::filesystem::path& active, std::string_view stored,
    bool environmentOverride);

} // namespace anime_vault
