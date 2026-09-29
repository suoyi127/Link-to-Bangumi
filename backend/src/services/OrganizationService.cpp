#include "anime_vault/services/OrganizationService.hpp"
#include "anime_vault/services/OrganizationExecutor.hpp"

#include <algorithm>
#include <limits>

namespace anime_vault {
namespace fs = std::filesystem;
namespace {
fs::path pathFromUtf8(const std::string& value) {
    const auto* bytes = reinterpret_cast<const char8_t*>(value.data());
    return fs::path(std::u8string_view(bytes, value.size()));
}
std::string pathToUtf8(const fs::path& value) {
    const auto bytes = value.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
bool validKey(const std::string& value) {
    return !value.empty() && value.size() <= 128 &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch >= 0x21 && ch <= 0x7e; });
}
}

OrganizationService::OrganizationService(MediaRepository& repository, fs::path qbRoot,
                                         fs::path importRoot, fs::path libraryRoot)
    : repository_(repository), qbRoot_(std::move(qbRoot)), importRoot_(std::move(importRoot)),
      libraryRoot_(std::move(libraryRoot)) {}

ExecuteOrganizationResult OrganizationService::execute(const ExecuteOrganizationRequest& request) {
    std::lock_guard lock(mutex_);
    if (!request.confirmed) throw OrganizationServiceError("confirmation_required");
    if (request.planId <= 0) throw OrganizationServiceError("invalid_plan_id");
    if (!validKey(request.idempotencyKey)) throw OrganizationServiceError("invalid_idempotency_key");
    // 重试沿用幂等键返回原任务，避免一次确认被重复执行成多个文件操作。
    if (const auto previous = repository_.findOrganizationJobByKey(request.idempotencyKey)) {
        if (previous->planId != request.planId)
            throw OrganizationServiceError("idempotency_key_reused");
        if (previous->status == "completed")
            return {previous->id, previous->resultPath, previous->bytes, previous->status};
        if (previous->status == "failed")
            throw OrganizationServiceError(previous->failureCode);
        throw OrganizationServiceError("execution_in_progress");
    }
    const auto now = std::chrono::steady_clock::now();
    while (!recentNewExecutions_.empty() &&
           now - recentNewExecutions_.front() >= std::chrono::seconds(1))
        recentNewExecutions_.pop_front();
    if (recentNewExecutions_.size() >= 10)
        throw OrganizationServiceError("execute_rate_limited");
    recentNewExecutions_.push_back(now);
    const auto plan = repository_.getPlan(request.planId);
    if (!plan) throw OrganizationServiceError("plan_not_found");
    const auto media = repository_.getMedia(plan->mediaFileId);
    if (!media) throw OrganizationServiceError("media_not_found");
    // 各来源使用独立根目录；只有 qB 源要求确认下载已经完成。
    if (media->origin == "qb_download") {
        if (!request.qbDownloadComplete) throw OrganizationServiceError("qb_completion_required");
    } else if (media->origin != "external_import" && media->origin != "folder_import") {
        throw OrganizationServiceError("invalid_origin");
    }
    if (plan->sourcePath != media->sourcePath || plan->sourceSize != media->sizeBytes ||
        plan->sourceModifiedAt != media->sourceModifiedAt)
        throw OrganizationServiceError("source_changed");
    if (plan->operation != "hardlink" && plan->operation != "copy" && plan->operation != "symlink")
        throw OrganizationServiceError("unsupported_operation");
    OrganizationJobRecord job;
    try { job = repository_.claimOrganization(plan->id, request.idempotencyKey); }
    catch (const OrganizationClaimError& error) { throw OrganizationServiceError(error.code()); }
    if (!job.newlyClaimed) {
        if (job.status == "completed") return {job.id, job.resultPath, job.bytes, job.status};
        if (job.status == "failed") throw OrganizationServiceError(job.failureCode);
        throw OrganizationServiceError("execution_in_progress");
    }
    fs::path sourcePath;
    fs::path targetPath;
    fs::file_time_type modified;
    try {
        if (plan->sourceSize < 0) throw std::invalid_argument("negative source size");
        std::size_t parsed = 0;
        const auto modifiedCount = std::stoll(plan->sourceModifiedAt, &parsed);
        if (parsed != plan->sourceModifiedAt.size())
            throw std::invalid_argument("invalid modification timestamp");
        modified = fs::file_time_type(fs::file_time_type::duration(modifiedCount));
        sourcePath = pathFromUtf8(plan->sourcePath);
        targetPath = pathFromUtf8(plan->targetPath);
    } catch (const std::exception&) {
        repository_.failOrganization(job.id, "invalid_plan");
        throw OrganizationServiceError("invalid_plan");
    }
    try {
        fs::path sourceRoot = media->origin == "external_import" ? importRoot_ : qbRoot_;
        if (media->origin == "folder_import") {
            const auto folder = media->folderImportId
                ? repository_.getFolderImport(*media->folderImportId) : std::nullopt;
            if (!folder) throw OrganizationServiceError("invalid_origin");
            sourceRoot = pathFromUtf8(folder->rootPath);
            std::error_code error;
            if (!fs::is_directory(sourceRoot, error) || error ||
                fs::canonical(sourceRoot, error) != sourceRoot || error)
                throw OrganizationServiceError("invalid_root");
        }
        OrganizationExecutor executor(sourceRoot, libraryRoot_);
        const auto result = executor.execute({sourcePath, targetPath,
            static_cast<std::uintmax_t>(plan->sourceSize),
            modified, plan->operation});
        if (result.bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max()))
            throw OrganizationServiceError("source_changed");
        const auto resultPath = pathToUtf8(result.target);
        repository_.completeOrganization(job.id, resultPath, static_cast<std::int64_t>(result.bytes));
        return {job.id, resultPath, static_cast<std::int64_t>(result.bytes), "completed"};
    } catch (const OrganizationError& error) {
        repository_.failOrganization(job.id, error.code());
        throw OrganizationServiceError(error.code());
    } catch (const OrganizationServiceError& error) {
        repository_.failOrganization(job.id, error.code());
        throw;
    } catch (const std::exception&) {
        // A database completion error may follow successful publication. Leave the
        // claimed job unresolved so a later reconciliation can prove target identity.
        throw OrganizationServiceError("execution_in_progress");
    }
}

} // namespace anime_vault
