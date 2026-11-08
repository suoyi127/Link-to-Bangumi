#pragma once
#include <memory>
namespace anime_vault {
class WatchSyncService;
namespace api {
void registerWatchSyncEndpoints(std::shared_ptr<WatchSyncService> service);
}
} // namespace anime_vault
