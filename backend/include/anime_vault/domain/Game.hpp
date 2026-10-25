#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace anime_vault {
struct GameError : std::runtime_error {
    explicit GameError(std::string value) : std::runtime_error(value), code(std::move(value)) {}
    std::string code;
};
struct GameResource {
    std::int64_t id{};
    std::string path, title, developer, summary, platform, vndbId, metadataSource;
    std::optional<std::int64_t> subjectId;
    bool manualMetadata{}, hasCover{}, missing{};
};
}
