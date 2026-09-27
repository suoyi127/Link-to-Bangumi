#pragma once

#include "anime_vault/domain/ParseResult.hpp"

#include <string_view>

namespace anime_vault {

class FilenameParser {
public:
    [[nodiscard]] ParseResult parse(std::string_view filename) const;
};

}  // namespace anime_vault
