#pragma once
#include "anime_vault/ports/LibraryFileAccess.hpp"
namespace anime_vault {
class NativeLibraryFileAccess final : public LibraryFileAccess {
public:
    bool isLinked(const std::filesystem::path& path) const override;
    bool isNovelPathSafe(const std::filesystem::path& path) const override;
    bool isNovelFileAccessible(const std::filesystem::path& path) const override;
    bool isGameExecutableAccessible(const std::filesystem::path& path) const override;
    bool isReaderExecutableValid(const std::string& path) const override;
};
}
