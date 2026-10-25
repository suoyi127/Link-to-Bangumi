#pragma once

#include <string>

namespace anime_vault {

struct CachedCover {
    std::string bytes;
    std::string mimeType;
    std::string extension;
};

} // namespace anime_vault
