#include "anime_vault/application/QbCompletedScanCoordinator.hpp"

#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <utility>

namespace anime_vault {
namespace {
void waitForNextScan(std::stop_token stop, std::chrono::milliseconds duration) {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock lock(mutex);
    wake.wait_for(lock, stop, duration, [] { return false; });
}
}

QbCompletedScanCoordinator::QbCompletedScanCoordinator(Dependencies dependencies)
    : QbCompletedScanCoordinator(std::move(dependencies), Timing{}) {}

QbCompletedScanCoordinator::QbCompletedScanCoordinator(Dependencies dependencies, Timing timing, Wait wait)
    : dependencies_(std::move(dependencies)), timing_(timing),
      wait_(wait ? std::move(wait) : Wait{waitForNextScan}) {}

QbCompletedScanCoordinator::~QbCompletedScanCoordinator() { stop(); }

void QbCompletedScanCoordinator::start() {
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

void QbCompletedScanCoordinator::stop() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}

void QbCompletedScanCoordinator::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        wait_(stop, timing_.scanWait);
        if (stop.stop_requested()) break;
        if (!dependencies_.eligible()) continue;
        // 回调只持有本轮共享结果，超时或停止后仍可安全完成。
        auto promise = std::make_shared<std::promise<QbStatus>>();
        auto result = promise->get_future();
        dependencies_.inspect([promise](QbStatus status) { promise->set_value(std::move(status)); });
        bool ready = false;
        for (int waited = 0; waited < timing_.inspectAttempts && !stop.stop_requested(); ++waited) {
            if (result.wait_for(timing_.inspectPoll) == std::future_status::ready) { ready = true; break; }
        }
        if (!ready || stop.stop_requested()) continue;
        const auto status = result.get();
        if (!status.connected) continue;
        try {
            std::vector<std::filesystem::path> completed;
            for (const auto& name : status.completedPaths)
                completed.emplace_back(std::u8string(name.begin(), name.end()));
            const auto scan = dependencies_.scanCompleted(completed);
            if (scan.status == "failed") dependencies_.log("automatic_scan_failed");
            // 新扫描始终追加补全；无新文件时仅空闲状态允许重试旧条目。
            int idle = 0;
            const bool schedule = scan.id > 0 || enrichmentActive_->compare_exchange_strong(idle, 1);
            if (scan.id > 0) enrichmentActive_->fetch_add(1);
            if (schedule) {
                // 排队任务与完成回调不依赖协调器的生命周期。
                dependencies_.post([enrich = dependencies_.enrich, active = enrichmentActive_, scanId = scan.id] {
                    enrich(scanId, [active] { active->fetch_sub(1); });
                });
            }
        } catch (const std::exception& error) {
            dependencies_.log(std::string("automatic_scan_unavailable: ") + error.what());
        }
    }
}
}
