#pragma once

#include "anime_vault/domain/ParseResult.hpp"

#include <string_view>

namespace anime_vault {

class FilenameParser {
public:
    // 从本地实际文件名解析标题与集数，不把 RSS 文章标题当作文件名来源。
    [[nodiscard]] ParseResult parse(std::string_view filename) const;
};

}  // namespace anime_vault
