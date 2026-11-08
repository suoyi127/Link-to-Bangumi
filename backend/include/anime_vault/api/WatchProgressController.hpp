#pragma once
namespace anime_vault {
class WatchProgressStore;
namespace api {
void registerWatchProgressEndpoints(WatchProgressStore &store);
}
} // namespace anime_vault
