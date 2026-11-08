#pragma once
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/services/MikanDownloads.hpp"
namespace anime_vault {
class MikanDownloadStore {
  public:
    explicit MikanDownloadStore(SqliteDatabase &db) : db_(db) {}
    void subscribe(const std::string &endpoint, const MikanSubscription &feed);
    void adoptRule(const std::string &endpoint, const std::string &feed, const std::set<std::string> &urls,
                   const std::map<std::string, std::set<std::string>> &history, const MikanAliases &aliases);
    std::set<std::string> baseline(const std::string &endpoint, const std::string &feed);
    std::vector<MikanSubscription> feeds(const std::string &endpoint);
    void learnSubtitleScripts(const std::string &endpoint, const std::string &feed,
                              const std::map<std::string, int> &observed);
    bool wanted(const std::string &endpoint, const MikanSubscription &feed, const MikanCandidate &candidate);
    void record(const std::string &endpoint, const MikanSubscription &feed, const MikanCandidate &candidate);
    MikanAliases aliases(const std::string &endpoint, const std::string &feed);
    void saveAliases(const std::string &endpoint, const std::string &feed, const MikanAliases &known);
    bool reserve(const std::string &endpoint, const MikanSubscription &feed, const MikanCandidate &candidate);
    std::vector<std::pair<MikanCandidate, bool>> pending(const std::string &endpoint, const MikanSubscription &feed);
    void retry(const std::string &endpoint, const MikanCandidate &candidate, int delay);
    void forget(const std::string &endpoint, const std::string &url);

  private:
    SqliteDatabase &db_;
};
} // namespace anime_vault
