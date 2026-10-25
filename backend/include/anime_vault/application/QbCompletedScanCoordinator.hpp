#pragma once

#include "anime_vault/domain/MediaRecords.hpp"
#include "anime_vault/domain/Qb.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <thread>

namespace anime_vault {
class QbCompletedScanCoordinator final {
public:
    using Inspect = std::function<void(std::function<void(QbStatus)>)>;
    using ScanCompleted = std::function<ScanRecord(const std::vector<std::filesystem::path>&)>;
    using Post = std::function<void(std::function<void()>)>;
    using Enrich = std::function<void(std::int64_t, std::function<void()>)>;
    using Wait = std::function<void(std::stop_token, std::chrono::milliseconds)>;

    struct Dependencies {
        std::function<bool()> eligible;
        Inspect inspect;
        ScanCompleted scanCompleted;
        Post post;
        Enrich enrich;
        std::function<void(const std::string&)> log;
    };
    struct Timing {
        std::chrono::milliseconds scanWait{std::chrono::seconds(10)};
        std::chrono::milliseconds inspectPoll{std::chrono::seconds(1)};
        int inspectAttempts{25};
    };

    explicit QbCompletedScanCoordinator(Dependencies dependencies);
    QbCompletedScanCoordinator(Dependencies dependencies, Timing timing, Wait wait = {});
    ~QbCompletedScanCoordinator();
    void start();
    void stop();
    void run(std::stop_token stop);

private:
    Dependencies dependencies_;
    Timing timing_;
    Wait wait_;
    std::shared_ptr<std::atomic_int> enrichmentActive_{std::make_shared<std::atomic_int>(0)};
    std::jthread worker_;
};
}
