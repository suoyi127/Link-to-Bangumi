#include "anime_vault/application/QbCompletedScanCoordinator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

using anime_vault::QbCompletedScanCoordinator;
using namespace std::chrono_literals;

namespace {
struct Scenario {
    std::stop_source stop;
    int cycles{1}, waits{}, inspections{}, scans{};
    bool configured{true}, directoryReady{true};
    anime_vault::QbStatus status{true, true};
    anime_vault::ScanRecord record{17, "", "completed"};
    std::vector<std::filesystem::path> paths;
    std::vector<std::function<void()>> posted;
    std::vector<std::int64_t> enriched;
    std::vector<std::function<void()>> done;
    std::vector<std::string> logs;

    QbCompletedScanCoordinator::Dependencies dependencies() {
        return {
            [&] { return configured && directoryReady; },
            [&](auto callback) { ++inspections; callback(status); },
            [&](const auto& completed) { ++scans; paths = completed; return record; },
            [&](auto callback) { posted.push_back(std::move(callback)); },
            [&](auto id, auto callback) { enriched.push_back(id); done.push_back(std::move(callback)); },
            [&](const auto& text) { logs.push_back(text); }
        };
    }
    QbCompletedScanCoordinator::Wait wait() {
        return [&](auto, auto) { if (++waits > cycles) stop.request_stop(); };
    }
    static QbCompletedScanCoordinator::Timing timing() { return {10s, 0ms, 2}; }
    void executePosted() {
        auto callbacks = std::move(posted);
        posted.clear();
        for (auto& callback : callbacks) callback();
    }
};
}

TEST_CASE("completed scan coordinator skips unconfigured or pending directories") {
    for (const bool pendingDirectory : {false, true}) {
        Scenario s;
        s.configured = pendingDirectory;
        s.directoryReady = !pendingDirectory;
        QbCompletedScanCoordinator coordinator(s.dependencies(), s.timing(), s.wait());
        coordinator.run(s.stop.get_token());
        CHECK(s.inspections == 0);
        CHECK(s.scans == 0);
    }
}

TEST_CASE("completed scan coordinator skips disconnected qB status") {
    Scenario s;
    s.status.connected = false;
    QbCompletedScanCoordinator coordinator(s.dependencies(), s.timing(), s.wait());
    coordinator.run(s.stop.get_token());
    CHECK(s.inspections == 1);
    CHECK(s.scans == 0);
    CHECK(s.posted.empty());
}

TEST_CASE("completed scan coordinator preserves UTF8 paths and enriches new scans") {
    Scenario s;
    s.status.completedPaths = {"D:/下载/番剧/01.mkv"};
    QbCompletedScanCoordinator coordinator(s.dependencies(), s.timing(), s.wait());
    coordinator.run(s.stop.get_token());
    REQUIRE(s.paths.size() == 1);
    CHECK(s.paths.front().u8string() == u8"D:/下载/番剧/01.mkv");
    REQUIRE(s.posted.size() == 1);
    s.executePosted();
    CHECK(s.enriched == std::vector<std::int64_t>{17});
    s.done.front()();
}

TEST_CASE("completed scan coordinator retries idle enrichment without new files") {
    Scenario s;
    s.record.id = 0;
    s.cycles = 3;
    auto dependencies = s.dependencies();
    dependencies.post = [&](auto callback) { callback(); };
    dependencies.enrich = [&](auto id, auto done) { s.enriched.push_back(id); done(); };
    QbCompletedScanCoordinator coordinator(std::move(dependencies), s.timing(), s.wait());
    coordinator.run(s.stop.get_token());
    CHECK(s.scans == 3);
    CHECK(s.enriched == std::vector<std::int64_t>{0, 0, 0});
}

TEST_CASE("completed scan coordinator deduplicates active retries but always adds new scans") {
    Scenario s;
    s.cycles = 5;
    auto dependencies = s.dependencies();
    dependencies.scanCompleted = [&](const auto&) {
        ++s.scans;
        s.record.id = s.scans == 2 || s.scans == 3 ? 17 : 0;
        if (s.scans == 5) {
            s.executePosted();
            for (auto& callback : s.done) callback();
        }
        return s.record;
    };
    QbCompletedScanCoordinator coordinator(std::move(dependencies), s.timing(), s.wait());
    coordinator.run(s.stop.get_token());
    CHECK(s.enriched == std::vector<std::int64_t>{0, 17, 17});
    REQUIRE(s.posted.size() == 1);
    s.executePosted();
    CHECK(s.enriched.back() == 0);
    s.done.back()();
}

TEST_CASE("completed scan coordinator logs failed scans and scan exceptions") {
    Scenario s;
    s.cycles = 2;
    auto dependencies = s.dependencies();
    dependencies.scanCompleted = [&](const auto&) {
        if (++s.scans == 2) throw std::runtime_error("scan problem");
        auto failed = s.record;
        failed.status = "failed";
        return failed;
    };
    QbCompletedScanCoordinator coordinator(std::move(dependencies), s.timing(), s.wait());
    coordinator.run(s.stop.get_token());
    CHECK(s.logs == std::vector<std::string>{"automatic_scan_failed", "automatic_scan_unavailable: scan problem"});
    REQUIRE(s.posted.size() == 1);
}

TEST_CASE("completed scan coordinator times out and safely accepts late inspect callbacks") {
    Scenario s;
    std::function<void(anime_vault::QbStatus)> late;
    auto dependencies = s.dependencies();
    dependencies.inspect = [&](auto callback) { late = std::move(callback); };
    {
        QbCompletedScanCoordinator coordinator(std::move(dependencies), s.timing(), s.wait());
        coordinator.run(s.stop.get_token());
        REQUIRE(s.scans == 0);
    }
    REQUIRE(late);
    REQUIRE_NOTHROW(late(s.status));
    CHECK(s.scans == 0);
}

TEST_CASE("completed scan coordinator stop after inspect prevents scanning") {
    Scenario s;
    std::function<void(anime_vault::QbStatus)> late;
    auto dependencies = s.dependencies();
    dependencies.inspect = [&](auto callback) { late = std::move(callback); s.stop.request_stop(); };
    {
        QbCompletedScanCoordinator coordinator(std::move(dependencies), s.timing(), s.wait());
        coordinator.run(s.stop.get_token());
    }
    REQUIRE_NOTHROW(late(s.status));
    CHECK(s.scans == 0);
}

TEST_CASE("queued enrichment and completion remain valid after coordinator destruction") {
    Scenario s;
    {
        QbCompletedScanCoordinator coordinator(s.dependencies(), s.timing(), s.wait());
        coordinator.run(s.stop.get_token());
    }
    s.executePosted();
    REQUIRE(s.done.size() == 1);
    REQUIRE_NOTHROW(s.done.front()());
}

TEST_CASE("completed scan coordinator destruction stops and joins its waiting worker") {
    Scenario s;
    std::mutex mutex;
    std::condition_variable ready;
    bool waiting = false;
    {
        QbCompletedScanCoordinator coordinator(s.dependencies(), s.timing(), [&](auto token, auto) {
            {
                std::lock_guard lock(mutex);
                waiting = true;
            }
            ready.notify_one();
            std::mutex waitMutex;
            std::condition_variable_any wake;
            std::unique_lock lock(waitMutex);
            wake.wait(lock, token, [] { return false; });
        });
        coordinator.start();
        std::unique_lock lock(mutex);
        REQUIRE(ready.wait_for(lock, 1s, [&] { return waiting; }));
    }
    CHECK(s.inspections == 0);
}

TEST_CASE("eligibility and inspect exceptions retain their caller boundary") {
    Scenario s;
    auto dependencies = s.dependencies();
    SECTION("eligibility") { dependencies.eligible = []() -> bool { throw std::runtime_error("eligible"); }; }
    SECTION("inspect") { dependencies.inspect = [](auto) { throw std::runtime_error("inspect"); }; }
    QbCompletedScanCoordinator coordinator(std::move(dependencies), s.timing(), s.wait());
    CHECK_THROWS_AS(coordinator.run(s.stop.get_token()), std::runtime_error);
    CHECK(s.logs.empty());
}
