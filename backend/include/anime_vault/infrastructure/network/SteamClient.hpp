#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace anime_vault {
struct SteamConfig { std::string steamPath, steamId, apiKey; };
class SteamConfiguration {
public:
    explicit SteamConfiguration(std::filesystem::path dataPath);
    SteamConfig current() const;
    void save(SteamConfig config);
    static void validate(const SteamConfig& config);
private:
    std::wstring target_;
    mutable std::mutex mutex_;
    SteamConfig config_;
};
struct SteamStats {
    std::optional<std::int64_t> playtimeMinutes, achievementsUnlocked, achievementsTotal;
    std::string errorCode;
};
class SteamClient {
public:
    using Completion = std::function<void(std::map<std::string, SteamStats>)>;
    using Request = std::function<void(std::string, std::function<void(int,std::string)>)>;
    explicit SteamClient(Request request);
    void fetch(SteamConfig config, std::vector<std::string> appIds, Completion completion);
private:
    Request request_;
};
}
