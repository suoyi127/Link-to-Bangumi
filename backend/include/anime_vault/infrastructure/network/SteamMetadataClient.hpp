#pragma once
#include "anime_vault/domain/CachedCover.hpp"
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace anime_vault {
struct SteamMetadata {
    std::string chineseTitle;
    std::optional<CachedCover> cover;
    std::vector<std::string> warnings;
};
class SteamMetadataClient {
public:
    using Request = std::function<void(std::string, std::string, std::function<void(int, std::string)>)>;
    using Completion = std::function<void(std::map<std::string, SteamMetadata>)>;
    explicit SteamMetadataClient(Request request) : request_(std::move(request)) {}
    void fetch(std::vector<std::string> appIds, Completion completion);
    static std::optional<std::pair<std::string, std::string>> coverRequest(const std::string& url, const std::string& appId);
private:
    Request request_;
};
}
