#pragma once
#include <filesystem>
#include <string>
namespace anime_vault {
class LibraryFileAccess {
public:
    virtual ~LibraryFileAccess() = default;
    virtual bool isLinked(const std::filesystem::path& path) const = 0;
    virtual bool isNovelPathSafe(const std::filesystem::path& path) const = 0;
    virtual bool isNovelFileAccessible(const std::filesystem::path& path) const = 0;
    virtual bool isGameExecutableAccessible(const std::filesystem::path& path) const = 0;
    virtual bool isReaderExecutableValid(const std::string& path) const = 0;
};
}
