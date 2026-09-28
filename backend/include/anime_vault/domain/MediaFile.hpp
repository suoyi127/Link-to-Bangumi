#pragma once

#include <cstdint>
#include <filesystem>

namespace anime_vault {

// 扫描器确认稳定后交给业务层的文件快照，用于执行前检查来源是否变化。
struct SourceFile {
    std::filesystem::path path;
    std::uintmax_t size{};
    std::filesystem::file_time_type modifiedAt{};
};

}  // namespace anime_vault
