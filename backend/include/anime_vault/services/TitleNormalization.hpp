#pragma once

#include <string>
#include <string_view>

namespace anime_vault {

// Shared exact-title key for matching, aliases and local lookups. An invalid or
// overlong UTF-8 title has no key, so it cannot accidentally match another title.
inline std::u32string normalizeTitle(std::string_view text) {
    std::u32string result;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        char32_t point = first;
        std::size_t width = 1;
        if (first >= 0x80 && first < 0xc2) return {};
        if ((first & 0xe0) == 0xc0) { point = first & 0x1f; width = 2; }
        else if ((first & 0xf0) == 0xe0) { point = first & 0x0f; width = 3; }
        else if ((first & 0xf8) == 0xf0) { point = first & 0x07; width = 4; }
        else if (first >= 0x80) return {};
        bool valid = i + width <= text.size();
        for (std::size_t j = 1; valid && j < width; ++j) {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            valid = (continuation & 0xc0) == 0x80;
            if (valid) point = (point << 6) | (continuation & 0x3f);
        }
        if (!valid || (width == 2 && point < 0x80) ||
            (width == 3 && point < 0x800) || (width == 4 && point < 0x10000) ||
            point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return {};
        i += width;
        if (point >= U'A' && point <= U'Z') point += U'a' - U'A';
        if (point >= 0xff10 && point <= 0xff19) point = U'0' + point - 0xff10;
        if (point >= 0xff21 && point <= 0xff3a) point = U'a' + point - 0xff21;
        if (point >= 0xff41 && point <= 0xff5a) point = U'a' + point - 0xff41;
        const bool asciiLetter = point >= U'a' && point <= U'z';
        const bool asciiDigit = point >= U'0' && point <= U'9';
        const bool separator = point == 0x00a0 || point == 0x1680 ||
            (point >= 0x2000 && point <= 0x206f) ||
            (point >= 0x3000 && point <= 0x3002) ||
            (point >= 0x3008 && point <= 0x301f) ||
            point == 0x3030 || point == 0x30fb || point == 0xfeff ||
            (point >= 0xff01 && point <= 0xff0f) ||
            (point >= 0xff1a && point <= 0xff20) ||
            (point >= 0xff3b && point <= 0xff40) ||
            (point >= 0xff5b && point <= 0xff65);
        if (asciiLetter || asciiDigit || (point >= 0x80 && !separator)) {
            if (result.size() == 256) return {};
            result.push_back(point);
        }
    }
    return result;
}

inline std::string normalizedTitleKey(std::string_view text) {
    const auto points = normalizeTitle(text);
    std::string key;
    for (char32_t point : points) {
        if (point < 0x80) key.push_back(static_cast<char>(point));
        else if (point < 0x800) {
            key.push_back(static_cast<char>(0xc0 | (point >> 6)));
            key.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        } else if (point < 0x10000) {
            key.push_back(static_cast<char>(0xe0 | (point >> 12)));
            key.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
            key.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        } else {
            key.push_back(static_cast<char>(0xf0 | (point >> 18)));
            key.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3f)));
            key.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f)));
            key.push_back(static_cast<char>(0x80 | (point & 0x3f)));
        }
    }
    return key;
}

} // namespace anime_vault
