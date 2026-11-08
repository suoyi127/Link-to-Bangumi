#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace anime_vault {
class SqliteDatabase;
struct WatchResponse {
    int status{};
    std::string body;
};
struct WatchSyncSettings {
    bool enabled{};
    std::optional<std::string> token;
};
struct WatchSyncIssue {
    std::int64_t mediaId{};
    std::string errorCode, title, episodeNumber, episodeType;
};
struct WatchSyncStatus {
    bool enabled{}, connected{};
    std::string username, errorCode;
    std::int64_t pending{};
    std::vector<WatchSyncIssue> issues;
};
struct WatchSyncError : std::runtime_error {
    WatchSyncError(int status, const std::string& code) : std::runtime_error(code), status(status) {}
    int status;
};

class WatchSyncService {
  public:
    using Request = std::function<WatchResponse(const std::string&, const std::string&, const std::string&,
                                                const std::string&)>;
    using Read = std::function<std::string(std::int64_t)>;
    using Save = std::function<void(std::int64_t, const std::string&)>;
    WatchSyncService(SqliteDatabase& db, Request request, Read read, Save save);
    ~WatchSyncService();
    WatchSyncStatus status() const;
    WatchSyncStatus configure(const WatchSyncSettings& settings);
    WatchSyncStatus collect(std::int64_t mediaId);
    void processOne();
    bool post(std::function<void()> task);
    void start();
    void stop();

  private:
    SqliteDatabase& db_;
    Request request_;
    Read read_;
    Save save_;
    std::mutex operationMutex_, waitMutex_;
    std::condition_variable_any wake_;
    std::deque<std::function<void()>> actions_;
    bool accepting_{};
    std::jthread worker_;
};
std::shared_ptr<WatchSyncService> makeWatchSyncService(SqliteDatabase& database, const std::filesystem::path& data,
                                                       std::function<std::string()> userAgent);
} // namespace anime_vault
