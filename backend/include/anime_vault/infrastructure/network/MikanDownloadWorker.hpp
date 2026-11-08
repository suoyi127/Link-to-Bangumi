#pragma once
#include "anime_vault/infrastructure/database/MikanDownloadStore.hpp"
#include "anime_vault/infrastructure/network/QbConnectionManager.hpp"
#include <condition_variable>
#include <deque>
#include <thread>
namespace anime_vault {
struct MikanRuleSync {
    int eligible{}, migrated{}, retained{}, alreadyManaged{}, unavailable{}, historicalEpisodes{}, baselineArticles{},
        aliases{};
    std::string errorCode;
};
class MikanDownloadWorker {
  public:
    MikanDownloadWorker(SqliteDatabase &db, std::shared_ptr<QbConnectionManager> manager, std::string savePath,
                        std::function<void()> validate);
    ~MikanDownloadWorker();
    void subscribe(MikanSubscription feed, QbWebClient::ActionCompletion complete);
    using SyncCompletion = std::function<void(MikanRuleSync)>;
    void synchronize(bool preview, SyncCompletion complete);
    void stop();

  private:
    struct Job {
        MikanSubscription feed;
        QbWebClient::ActionCompletion complete;
        SyncCompletion onSync;
        bool preview{};
    };
    void run(std::stop_token token);
    void syncRules(bool preview, MikanRuleSync &result);
    void refresh(const std::optional<Job> &job, std::stop_token token);
    MikanDownloadStore store_;
    std::shared_ptr<QbConnectionManager> manager_;
    std::string savePath_;
    std::function<void()> validate_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<Job> jobs_;
    std::jthread thread_;
};
} // namespace anime_vault
