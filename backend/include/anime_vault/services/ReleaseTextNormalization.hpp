#pragma once
#include <string>
#include <string_view>

namespace anime_vault {
inline std::string normalizeReleaseText(std::string_view input) {
    std::string text(input);
    const auto replace = [&text](std::string_view from, std::string_view to) {
        std::size_t at = 0;
        while ((at = text.find(from, at)) != std::string::npos) {
            text.replace(at, from.size(), to); at += to.size();
        }
    };
    for (const auto space : {" ", " ", " ", " ", " ", " ", " ", " ", " ", " ", " ", " ", " ", " ", "　"}) replace(space, " ");
    for (const auto dash : {"－", "—", "–", "−"}) replace(dash, "-");
    replace("［", "["); replace("］", "]");
    replace("【", "["); replace("】", "]"); replace("／", "/");
    for (char& ch : text) if (ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
    return text;
}
}
