#pragma once

#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace anime_vault {
class WatchProgressStore;

class MpvProgressMonitor {
public:
    explicit MpvProgressMonitor(WatchProgressStore& store) : store_(store) {}
    ~MpvProgressMonitor();
    bool launch(ProcessLauncher& launcher, const std::filesystem::path& executable,
                const std::string& mediaPath, std::int64_t mediaId, bool fromStart);
private:
    struct Worker {
        std::shared_ptr<std::atomic_bool> done;
        std::jthread thread;
    };
    WatchProgressStore& store_;
    std::mutex mutex_;
    std::vector<Worker> workers_;
};
} // namespace anime_vault
