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
// 将默认媒体根目录放在应用项目根下，不依赖 Windows 用户个人资料所在盘符。
std::filesystem::path defaultMediaDirectory(const std::filesystem::path& applicationRoot);
// 合并环境配置与持久化偏好，并校验来源、导入、媒体库之间的目录边界。
RuntimePaths resolveRuntimePaths(std::filesystem::path data,
    std::filesystem::path imported, std::filesystem::path library,
    std::optional<std::filesystem::path> qbOverride, std::string_view storedQb);
std::filesystem::path validateQbDownloadDirectory(const std::filesystem::path& candidate,
    const std::filesystem::path& imported, const std::filesystem::path& library,
    const std::filesystem::path& data);
bool qbDirectoryActive(const std::filesystem::path& active, std::string_view stored,
    bool environmentOverride);

} // namespace anime_vault
