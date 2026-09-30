#include "anime_vault/ports/VndbTransport.hpp"
#include <regex>
namespace anime_vault {
std::optional<std::string> VndbTransport::allowedCoverPath(const std::string& url) {
    // 只接受 VNDB 的封面 CDN 路径，不请求 API 返回的任意地址。
    if (url.size() > 300 || !std::regex_match(url, std::regex("^https://t\\.vndb\\.org/cv/[0-9]{2}/[0-9]+\\.(jpg|png|webp)$"))) return {};
    return url.substr(std::string("https://t.vndb.org").size());
}
}
