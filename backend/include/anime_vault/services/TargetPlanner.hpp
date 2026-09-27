#pragma once

#include "anime_vault/domain/MediaFile.hpp"
#include "anime_vault/domain/ParseResult.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace anime_vault {

struct TargetPlan {
    std::filesystem::path source;
    std::filesystem::path target;
    std::string mode{"hardlink"};
    std::vector<std::string> conflicts;
};

class TargetPlanner {
public:
    explicit TargetPlanner(std::filesystem::path libraryRoot);
    [[nodiscard]] TargetPlan preview(const SourceFile& source, std::string_view displayTitle,
                                     EpisodeType type, const EpisodeNumber& episode,
                                     std::string_view versionLabel = {}) const;

private:
    std::filesystem::path requestedRoot_;
    std::filesystem::path root_;
};

}  // namespace anime_vault
