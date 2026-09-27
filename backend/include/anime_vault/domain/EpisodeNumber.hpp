#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace anime_vault {

class EpisodeNumber {
public:
    [[nodiscard]] static std::optional<EpisodeNumber> parse(std::string_view text);
    [[nodiscard]] static EpisodeNumber fromParts(std::uint32_t whole,
                                                 std::optional<std::uint32_t> tenth);

    [[nodiscard]] std::uint32_t whole() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> tenth() const noexcept;
    [[nodiscard]] std::string toString() const;

    auto operator<=>(const EpisodeNumber&) const = default;

private:
    std::uint32_t whole_{};
    std::optional<std::uint32_t> tenth_;
};

}  // namespace anime_vault
