#include "anime_vault/services/SteamPoster.hpp"
#include "anime_vault/services/CoverScraper.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <tuple>

namespace anime_vault {
namespace {
namespace fs = std::filesystem;
bool validId(const std::string &id) {
    return !id.empty() && id.size() <= 10 && id.front() != '0' &&
           std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}
} // namespace
bool usesSteamPoster(const GameResource &game) {
    return validId(game.steamAppId) && !game.subjectId && game.vndbId.empty() &&
           (game.metadataSource.empty() || game.metadataSource == "steam");
}
std::optional<CachedCover> readSteamPoster(const GameResource &game) {
    if (!usesSteamPoster(game) || game.steamExecutable.empty())
        return {};
    std::error_code ec;
    const auto executable = fs::u8path(game.steamExecutable);
    if (!executable.is_absolute())
        return {};
    const auto root = fs::canonical(executable.parent_path(), ec);
    if (ec)
        return {};
    const auto app = root / "appcache" / "librarycache" / game.steamAppId;
    // 只访问已登记 AppID 的缓存目录及一层资源哈希目录，拒绝越界链接。
    if (fs::canonical(app, ec) != app || ec)
        return {};
    using Candidate = std::tuple<int, fs::file_time_type, fs::path>;
    std::vector<Candidate> candidates;
    static const std::regex asset(R"(^library_(?:600x900|capsule)(?:_([a-z]+))?\.(?:jpg|jpeg|png|webp)$)",
                                  std::regex::icase);
    static const std::regex hash(R"(^[a-fA-F0-9]{40}$)");
    std::vector<fs::path> dirs{app};
    std::size_t inspected = 0;
    for (std::size_t i = 0; i < dirs.size() && inspected < 1024; ++i) {
        fs::directory_iterator it(dirs[i], ec), end;
        for (; !ec && it != end && inspected < 1024; it.increment(ec), ++inspected) {
            const auto p = it->path();
            const auto u8name = p.filename().u8string();
            const std::string name(u8name.begin(), u8name.end());
            std::error_code status;
            if (i == 0 && std::regex_match(name, hash) && it->is_directory(status) && fs::canonical(p, status) == p) {
                dirs.push_back(p);
                continue;
            }
            std::smatch match;
            if (!std::regex_match(name, match, asset) || !it->is_regular_file(status) || fs::canonical(p, status) != p)
                continue;
            const auto lang = match[1].str();
            const int priority = lang == "schinese"   ? 0
                                 : lang == "tchinese" ? 1
                                 : lang.empty()       ? 2
                                 : lang == "english"  ? 3
                                                      : 4;
            auto modified = it->last_write_time(status);
            if (!status)
                candidates.emplace_back(priority, modified, p);
        }
        ec.clear();
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (std::get<0>(a) != std::get<0>(b))
            return std::get<0>(a) < std::get<0>(b);
        if (std::get<1>(a) != std::get<1>(b))
            return std::get<1>(a) > std::get<1>(b);
        return std::get<2>(a) < std::get<2>(b);
    });
    for (const auto &[priority, modified, path] : candidates) {
        const auto size = fs::file_size(path, ec);
        if (ec || size < 4 || size > 5 * 1024 * 1024)
            continue;
        std::ifstream input(path, std::ios::binary);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (!input.read(bytes.data(), static_cast<std::streamsize>(size)))
            continue;
        if (auto image = CoverScraper::detectImage(bytes)) {
            image->bytes = std::move(bytes);
            return image;
        }
    }
    return {};
}
} // namespace anime_vault
