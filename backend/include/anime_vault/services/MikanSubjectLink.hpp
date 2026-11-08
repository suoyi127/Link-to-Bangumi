#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace anime_vault {
std::optional<std::int64_t> mikanSeriesId(const std::string& feedUrl);
std::optional<std::int64_t> mikanLinkedSubject(std::string_view html);
}
