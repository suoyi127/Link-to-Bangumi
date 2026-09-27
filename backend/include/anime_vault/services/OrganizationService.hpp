#pragma once

#include "anime_vault/repositories/MediaRepository.hpp"

#include <cstdint>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>

namespace anime_vault {

struct ExecuteOrganizationRequest {
    std::int64_t planId{};
    std::string idempotencyKey;
    bool confirmed{};
    bool qbDownloadComplete{};
};

struct ExecuteOrganizationResult {
    std::int64_t jobId{};
    std::string targetPath;
    std::int64_t bytes{};
    std::string status;
};

class OrganizationServiceError final : public std::runtime_error {
public:
    explicit OrganizationServiceError(std::string code)
        : std::runtime_error(code), code_(std::move(code)) {}
    [[nodiscard]] const std::string& code() const noexcept { return code_; }
private:
    std::string code_;
};

class OrganizationService {
public:
    OrganizationService(MediaRepository& repository, std::filesystem::path qbRoot,
                        std::filesystem::path importRoot, std::filesystem::path libraryRoot);
    ExecuteOrganizationResult execute(const ExecuteOrganizationRequest& request);
private:
    MediaRepository& repository_;
    std::filesystem::path qbRoot_;
    std::filesystem::path importRoot_;
    std::filesystem::path libraryRoot_;
    std::mutex mutex_;
    std::deque<std::chrono::steady_clock::time_point> recentNewExecutions_;
};

} // namespace anime_vault
