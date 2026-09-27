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

    std::vector<SourceFile> scan(std::chrono::seconds stableFor,
                                 std::filesystem::file_time_type now);
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
