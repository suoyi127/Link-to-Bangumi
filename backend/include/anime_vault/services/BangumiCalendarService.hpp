#pragma once

#include "anime_vault/ports/BangumiTransport.hpp"
#include "anime_vault/repositories/MediaRepository.hpp"

#include <array>
#include <chrono>
#include <functional>
#include <memory>

namespace anime_vault {
struct AnimeCalendarDay {
    int weekday{};
    std::vector<AnimeRecord> items;
};
struct AnimeCalendarResult {
    std::array<AnimeCalendarDay, 7> days;
    bool fromCache{};
    bool stale{};
    std::int64_t updatedAt{};
    std::string errorCode;
};

// 缓存远端放送表，每次读取仍按最新本地媒体记录计算交集。
class BangumiCalendarService final : public std::enable_shared_from_this<BangumiCalendarService> {
public:
    using Clock = std::function<std::chrono::system_clock::time_point()>;
    using Completion = std::function<void(AnimeCalendarResult)>;
    BangumiCalendarService(MediaRepository& repository, BangumiTransport& transport,
        Clock clock = [] { return std::chrono::system_clock::now(); });
    void get(Completion completion);
private:
    AnimeCalendarResult match(const std::string& body) const;
    MediaRepository& repository_;
    BangumiTransport& transport_;
    Clock clock_;
};
}
