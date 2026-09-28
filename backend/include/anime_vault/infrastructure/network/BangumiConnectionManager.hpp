#pragma once

#include "anime_vault/ports/BangumiTransport.hpp"
#include "anime_vault/services/CoverScraper.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace anime_vault {
class SqliteDatabase;
class DrogonBangumiTransport;
class DrogonCoverTransport;

struct BangumiConfigSummary { std::string userAgent, source; bool configured{}; };
class BangumiConfigError final : public std::runtime_error {
public:
    BangumiConfigError(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code(std::move(code)) {}
    std::string code;
};

// 汇总 Bangumi 配置，并为条目查询与封面抓取提供统一传输实现。
class BangumiConnectionManager final : public BangumiTransport, public CoverImageFetcher {
public:
    BangumiConnectionManager(SqliteDatabase& database, std::string environmentUserAgent);
    BangumiConfigSummary summary() const;
    void save(std::string userAgent);
    void clear();
    static bool validUserAgent(const std::string& value);
    void search(std::string keyword, BangumiTransport::Completion completion) override;
    void subject(std::int64_t id, BangumiTransport::Completion completion) override;
    void fetch(std::string url, CoverImageFetcher::Completion completion) override;
private:
    struct Active {
        std::shared_ptr<DrogonBangumiTransport> api;
        std::shared_ptr<DrogonCoverTransport> covers;
    };
    static std::shared_ptr<Active> makeActive(const std::string& userAgent);
    std::string loadSaved() const;
    SqliteDatabase& database_;
    std::string environmentUserAgent_;
    mutable std::mutex mutex_;
    BangumiConfigSummary summary_;
    std::shared_ptr<Active> active_;
};
}
