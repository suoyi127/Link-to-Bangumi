#pragma once
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace anime_vault {
struct WatchSession {
    std::int64_t id{};
    double position{};
};
struct WatchProgress {
    std::int64_t mediaId{};
    bool watched{};
    double position{}, duration{};
    std::int64_t updatedAt{};
};
struct ContinueEpisode {
    std::int64_t animeId{}, mediaId{};
    std::string title, episodeNumber, episodeType;
    double position{}, duration{};
    bool nextEpisode{};
};
class WatchProgressStore {
  public:
    explicit WatchProgressStore(SqliteDatabase &db) : db_(db) {}
    std::optional<double> resumePosition(std::int64_t mediaId);
    WatchSession begin(std::int64_t mediaId, bool fromStart);
    void sample(std::int64_t mediaId, std::int64_t sessionId, double position, double duration,
                const std::vector<std::pair<double, double>> &playedRanges);
    WatchProgress mark(std::int64_t mediaId, bool watched);
    std::vector<WatchProgress> list(std::int64_t animeId);
    std::vector<ContinueEpisode> continuing();

  private:
    SqliteDatabase &db_;
};
} // namespace anime_vault
