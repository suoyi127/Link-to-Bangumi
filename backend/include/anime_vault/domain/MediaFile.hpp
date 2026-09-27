#pragma once

#include <cstdint>
#include <filesystem>

namespace anime_vault {

struct SourceFile {
    std::filesystem::path path;
    std::uintmax_t size{};
    std::filesystem::file_time_type modifiedAt{};
};

}  // namespace anime_vault
