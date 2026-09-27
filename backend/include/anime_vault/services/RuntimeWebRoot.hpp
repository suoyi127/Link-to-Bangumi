#pragma once

#include <filesystem>

namespace anime_vault {
std::filesystem::path validatedWebRoot(const std::filesystem::path& candidate);
}
