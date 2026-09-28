#pragma once

#include "anime_vault/domain/MediaFile.hpp"

#include <chrono>
#include <filesystem>
#include <map>
#include <vector>

namespace anime_vault {

class DirectoryScanner {
public:
    explicit DirectoryScanner(std::filesystem::path root);

    // 只返回大小和修改时间在稳定期内未变化的文件，避免处理仍在写入的下载内容。
    std::vector<SourceFile> scan(std::chrono::seconds stableFor,
                                 std::filesystem::file_time_type now);
    // 本轮观察路径用于将数据库中已消失的来源文件标记为 missing。
    std::vector<std::filesystem::path> observedPaths() const;

private:
    struct Snapshot {
        std::uintmax_t size;
        std::filesystem::file_time_type modifiedAt;
        std::filesystem::file_time_type firstSeen;
    };
    std::filesystem::path root_;
    std::map<std::filesystem::path, Snapshot> snapshots_;
};

}  // namespace anime_vault
