#pragma once
#include "anime_vault/infrastructure/network/SteamClient.hpp"
#include "anime_vault/infrastructure/network/SteamMetadataClient.hpp"
#include "anime_vault/ports/GameRepository.hpp"
#include <atomic>
#include <functional>
#include <memory>
namespace anime_vault {
struct SteamImportResult {
    int imported{}, updated{};
    std::vector<std::string> warnings;
    std::string errorCode;
};
class SteamImportService : public std::enable_shared_from_this<SteamImportService> {
public:
    using Completion = std::function<void(SteamImportResult)>;
    SteamImportService(GameRepository& games, SteamClient& client, SteamMetadataClient* metadata = nullptr) : games_(games), client_(client), metadata_(metadata) {}
    void importInstalled(SteamConfig config, Completion completion);
    bool busy() const { return busy_.load(); }
    void stop();
private:
    GameRepository& games_;
    SteamClient& client_;
    SteamMetadataClient* metadata_{};
    void enrich(std::vector<GameResource> entries, SteamImportResult result, Completion completion);
    std::atomic_bool busy_{};
    std::recursive_mutex lifecycle_;
    bool stopped_{};
};
}
