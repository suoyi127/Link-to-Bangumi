#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace anime_vault {

enum class MediaFailureKind { invalidInput, missingResource, conflict };

class MediaError final : public std::runtime_error {
public:
    MediaError(MediaFailureKind kind, std::string code, std::string message)
        : std::runtime_error(std::move(message)), kind(kind), code(std::move(code)) {}
    MediaFailureKind kind;
    std::string code;
};

} // namespace anime_vault
