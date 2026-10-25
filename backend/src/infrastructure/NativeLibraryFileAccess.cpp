#include "anime_vault/infrastructure/NativeLibraryFileAccess.hpp"
#include "anime_vault/services/PlayerCatalog.hpp"
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
namespace anime_vault {
namespace fs = std::filesystem;
bool NativeLibraryFileAccess::isLinked(const fs::path& path) const {
    if (fs::is_symlink(path)) return true;
#ifdef _WIN32
    const auto attr = GetFileAttributesW(path.c_str());
    return attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return false;
#endif
}
bool NativeLibraryFileAccess::isNovelPathSafe(const fs::path& path) const {
    // 每一级都检查，防止登记后父目录被替换为 junction 后越界打开。
    for (auto p = path; !p.empty(); p = p.parent_path()) {
        if (isLinked(p)) return false;
        if (p == p.root_path()) break;
    }
    return true;
}
bool NativeLibraryFileAccess::isNovelFileAccessible(const fs::path& path) const {
    std::error_code pathError;
    const auto status = fs::symlink_status(path, pathError);
    return !(pathError || !fs::is_regular_file(status) || !isNovelPathSafe(path) ||
        !std::ifstream(path, std::ios::binary).is_open());
}
bool NativeLibraryFileAccess::isGameExecutableAccessible(const fs::path& path) const {
    std::error_code error;
    if (!path.is_absolute() || !fs::is_regular_file(path, error) || error) return false;
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".exe") return false;
    // 游戏目录常依赖相对路径；拒绝重解析路径，启动时固定使用 EXE 所在目录。
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        if (fs::is_symlink(fs::symlink_status(current, error)) || error) return false;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
        if (current == current.root_path()) break;
    }
    std::ifstream file(path, std::ios::binary); char header[2]{};
    return static_cast<bool>(file.read(header, 2)) && header[0] == 'M' && header[1] == 'Z';
}
bool NativeLibraryFileAccess::isReaderExecutableValid(const std::string& path) const {
    return validPlayerExecutable(path);
}
}
