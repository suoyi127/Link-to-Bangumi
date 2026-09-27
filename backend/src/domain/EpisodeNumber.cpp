// Active: 1781657419783@@127.0.0.1@3306@mysql
#include "anime_vault/domain/EpisodeNumber.hpp"

#include <charconv>
#include <stdexcept>
#include <system_error>

namespace anime_vault {

std::optional<EpisodeNumber> EpisodeNumber::parse(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }

    const auto dot = text.find('.');
    const auto wholeText = text.substr(0, dot);
    if (wholeText.empty()) {
        return std::nullopt;
    }

    std::uint32_t whole = 0;
    const auto [end, error] = std::from_chars(wholeText.data(),
                                              wholeText.data() + wholeText.size(), whole);
    if (error != std::errc{} || end != wholeText.data() + wholeText.size()) {
        return std::nullopt;
    }

    if (dot == std::string_view::npos) {
        return fromParts(whole, std::nullopt);
    }

    const auto decimal = text.substr(dot + 1);
    if (decimal.size() != 1 || decimal.front() < '0' || decimal.front() > '9') {
        return std::nullopt;
    }
    return fromParts(whole, static_cast<std::uint32_t>(decimal.front() - '0'));
}

EpisodeNumber EpisodeNumber::fromParts(std::uint32_t whole,
                                       std::optional<std::uint32_t> tenth) {
    if (tenth && *tenth > 9) {
        throw std::invalid_argument("episode tenth must be a single decimal digit");
    }
    EpisodeNumber number;
    number.whole_ = whole;
    number.tenth_ = tenth;
    return number;
}

std::uint32_t EpisodeNumber::whole() const noexcept {
    return whole_;
}

std::optional<std::uint32_t> EpisodeNumber::tenth() const noexcept {
    return tenth_;
}

std::string EpisodeNumber::toString() const {
    auto text = std::to_string(whole_);
    if (tenth_) {
        text.push_back('.');
        text.push_back(static_cast<char>('0' + *tenth_));
    }
    return text;
}

}  // namespace anime_vault
