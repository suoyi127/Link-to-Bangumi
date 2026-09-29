#include "anime_vault/api/MediaService.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cwctype>
#ifdef _WIN32
#include <windows.h>
#endif

namespace anime_vault::api {
namespace fs = std::filesystem;
namespace {
std::string utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
fs::path fromUtf8(const std::string& value) {
    const auto* bytes = reinterpret_cast<const char8_t*>(value.data());
    return fs::path(std::u8string_view(bytes, value.size()));
}
bool within(const fs::path& root, const fs::path& candidate) {
    auto parent = root.begin();
    auto child = candidate.begin();
    for (; parent != root.end(); ++parent, ++child) {
        if (child == candidate.end()) return false;
#ifdef _WIN32
        auto left = parent->wstring();
        auto right = child->wstring();
        std::transform(left.begin(), left.end(), left.begin(), std::towlower);
        std::transform(right.begin(), right.end(), right.begin(), std::towlower);
        if (left != right) return false;
#else
        if (*parent != *child) return false;
#endif
    }
    return true;
}
fs::path canonicalConfiguredPath(fs::path path) {
    path = fs::absolute(path).lexically_normal();
    fs::path suffix;
    std::error_code error;
    while (!fs::exists(path, error)) {
        if (path == path.root_path())
            throw ApiError(409, "root_configuration_invalid", "cannot resolve configured root");
        suffix = path.filename() / suffix;
        path = path.parent_path();
    }
    auto resolved = fs::canonical(path, error);
    if (error) throw ApiError(409, "root_configuration_invalid", "cannot resolve configured root");
#ifdef _WIN32
    const auto handle = CreateFileW(resolved.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw ApiError(409, "root_configuration_invalid", "cannot resolve configured root");
    const auto required = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    std::wstring finalPath(required, L'\0');
    const auto written = required == 0 ? 0 :
        GetFinalPathNameByHandleW(handle, finalPath.data(), required, FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if (written == 0 || written >= required)
        throw ApiError(409, "root_configuration_invalid", "cannot resolve configured root");
    finalPath.resize(written);
    if (finalPath.starts_with(L"\\\\?\\UNC\\"))
        finalPath = L"\\\\" + finalPath.substr(8);
    else if (finalPath.starts_with(L"\\\\?\\"))
        finalPath.erase(0, 4);
    resolved = fs::path(finalPath);
#endif
    auto normalized = (resolved / suffix).lexically_normal();
    if (!normalized.has_filename() && normalized != normalized.root_path())
        normalized = normalized.parent_path();
    return normalized;
}
void validateDistinctRoots(const fs::path& source, const fs::path& imported,
                           const fs::path& library) {
    const auto sourceRoot = canonicalConfiguredPath(source);
    const auto importRoot = canonicalConfiguredPath(imported);
    const auto libraryRoot = canonicalConfiguredPath(library);
    if (within(sourceRoot, importRoot) || within(importRoot, sourceRoot) ||
        within(sourceRoot, libraryRoot) || within(libraryRoot, sourceRoot) ||
        within(importRoot, libraryRoot) || within(libraryRoot, importRoot)) {
        throw ApiError(409, "overlapping_roots", "source, import and library roots must be separate");
    }
}
std::string typeName(EpisodeType type) {
    switch (type) {
    case EpisodeType::normal: return "normal";
    case EpisodeType::sp: return "sp";
    case EpisodeType::ova: return "ova";
    case EpisodeType::ncop: return "ncop";
    case EpisodeType::nced: return "nced";
    case EpisodeType::unknown: return "unknown";
    }
    return "unknown";
}
EpisodeType parseType(const std::string& text) {
    if (text == "normal") return EpisodeType::normal;
    if (text == "sp") return EpisodeType::sp;
    if (text == "ova") return EpisodeType::ova;
    if (text == "ncop") return EpisodeType::ncop;
    if (text == "nced") return EpisodeType::nced;
    throw ApiError(400, "invalid_episode_type", "invalid episode type");
}
std::string expiration() {
    auto now = std::chrono::system_clock::now() + std::chrono::minutes(15);
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
} // namespace

MediaService::MediaService(MediaRepository& repository, fs::path sourceRoot, fs::path libraryRoot,
                           fs::path importRoot, bool qbConfigured)
    : repository_(repository), qbConfigured_(qbConfigured), sourceRoot_(fs::canonical(sourceRoot)),
      sourceRequestedRoot_(fs::absolute(sourceRoot).lexically_normal()),
      importRoot_(fs::absolute(std::move(importRoot)).lexically_normal()),
      libraryRoot_(canonicalConfiguredPath(libraryRoot)), scanner_(std::move(sourceRoot)),
      planner_(std::move(libraryRoot)) {
    validateDistinctRoots(sourceRequestedRoot_, importRoot_, libraryRoot_);
}

ScanRecord MediaService::createScan(std::chrono::seconds stableFor) {
    const std::lock_guard lock(mutex_);
    validateQbSourceReady();
    enforceRateLimit();
    return scanFrom(scanner_, "qb_download", stableFor);
}

void MediaService::validateQbSourceReady() const {
    if (!qbConfigured_)
        throw ApiError(409, "qb_download_dir_unconfigured", "select a qB download directory first");
    validateSourceRoot();
    validateDistinctRoots(sourceRequestedRoot_, importRoot_, libraryRoot_);
}

ScanRecord MediaService::createImportScan(std::chrono::seconds stableFor) {
    const std::lock_guard lock(mutex_);
    std::error_code error;
    if (!fs::is_directory(importRoot_, error) || error)
        throw ApiError(409, "import_root_unavailable", "configured import root is unavailable");
    validateSourceRoot();
    validateDistinctRoots(sourceRequestedRoot_, importRoot_, libraryRoot_);
    const auto currentRoot = fs::canonical(importRoot_, error);
    if (error) throw ApiError(409, "import_root_unavailable", "configured import root is unavailable");
    if (importScanner_ && (!within(importScannerRoot_, currentRoot) ||
                           !within(currentRoot, importScannerRoot_)))
        throw ApiError(409, "import_root_changed", "configured import root changed after scanning");
    if (!importScanner_) {
        importScanner_ = std::make_unique<DirectoryScanner>(importRoot_);
        importScannerRoot_ = currentRoot;
    }
    enforceRateLimit();
    return scanFrom(*importScanner_, "external_import", stableFor);
}

FolderImportRecord MediaService::addFolderImport(const std::string& path) {
    const std::lock_guard lock(mutex_);
    if (path.empty() || path.size() > 2048 || path.find('\0') != std::string::npos)
        throw ApiError(400, "invalid_folder_path", "invalid folder path");
    fs::path requested;
    try { requested = fromUtf8(path); }
    catch (const fs::filesystem_error&) {
        throw ApiError(400, "invalid_folder_path", "invalid UTF-8 folder path");
    }
    if (!requested.is_absolute() || requested == requested.root_path())
        throw ApiError(400, "invalid_folder_path", "absolute non-root folder required");
    std::error_code error;
    if (!fs::is_directory(requested, error) || error)
        throw ApiError(409, "folder_unavailable", "folder is unavailable");
    const auto root = fs::canonical(requested, error);
    if (error || root == root.root_path())
        throw ApiError(400, "invalid_folder_path", "invalid folder root");
    const auto encoded = utf8(root);
    if (encoded.size() > 2048) throw ApiError(400, "invalid_folder_path", "folder path is too long");
    const auto folders = repository_.listFolderImports();
    for (const auto& folder : folders) {
        const auto existing = fromUtf8(folder.rootPath);
        if (existing == root) return folder;
        if (within(existing, root) || within(root, existing))
            throw ApiError(409, "overlapping_roots", "folder imports must be separate");
    }
    for (const auto& protectedRoot : {sourceRequestedRoot_, importRoot_, libraryRoot_}) {
        const auto resolved = canonicalConfiguredPath(protectedRoot);
        if (within(resolved, root) || within(root, resolved))
            throw ApiError(409, "overlapping_roots", "folder overlaps a managed source");
    }
    return repository_.addFolderImport(encoded);
}

std::vector<FolderImportRecord> MediaService::listFolderImports() const {
    const std::lock_guard lock(mutex_);
    return repository_.listFolderImports();
}

ScanRecord MediaService::createFolderScan(std::int64_t id) {
    const std::lock_guard lock(mutex_);
    const auto folder = repository_.getFolderImport(id);
    if (!folder) throw ApiError(404, "folder_import_not_found", "folder import not found");
    const auto root = fromUtf8(folder->rootPath);
    std::error_code error;
    if (!fs::is_directory(root, error) || error || fs::canonical(root, error) != root || error)
        throw ApiError(409, "folder_unavailable", "folder import root is unavailable or changed");
    enforceRateLimit();
    DirectoryScanner scanner(root);
    // 用户显式选择已有目录时立即读取，后续整理仍会核对文件大小与修改时间。
    return scanFrom(scanner, "folder_import", std::chrono::seconds{0}, id);
}

void MediaService::validateSourceRoot() const {
    std::error_code error;
    const auto currentRoot = fs::canonical(sourceRequestedRoot_, error);
    if (error || !fs::is_directory(currentRoot))
        throw ApiError(409, "source_root_unavailable", "configured source root is unavailable");
    if (!within(sourceRoot_, currentRoot) || !within(currentRoot, sourceRoot_))
        throw ApiError(409, "source_root_changed", "configured source root changed after scanning");
}

void MediaService::enforceRateLimit() {
    const auto now = std::chrono::steady_clock::now();
    if (lastScan_ != std::chrono::steady_clock::time_point{} &&
        now - lastScan_ < std::chrono::seconds(1)) {
        throw ApiError(409, "scan_rate_limited", "wait before scanning again");
    }
    lastScan_ = now;
}

ScanRecord MediaService::scanFrom(DirectoryScanner& scanner, const std::string& origin,
                                  std::chrono::seconds stableFor,
                                  std::optional<std::int64_t> folderImportId) {
    ScanRecord scan{0, origin, "running"};
    scan.id = repository_.createScan(scan);
    try {
        const auto files = scanner.scan(stableFor, fs::file_time_type::clock::now());
        scan.discoveredCount = static_cast<std::int64_t>(files.size());
        for (const auto& file : files) {
            const auto parsed = parser_.parse(utf8(file.path.filename()));
            MediaRecord media{};
            media.scanId = scan.id;
            media.sourcePath = utf8(file.path);
            media.filename = utf8(file.path.filename());
            media.parsedTitle = parsed.title;
            media.episodeNumber = parsed.episode ? parsed.episode->toString() : "";
            media.episodeType = typeName(parsed.episodeType);
            media.sizeBytes = static_cast<std::int64_t>(file.size);
            media.status = "inbox";
            media.confidence = parsed.confidence;
            media.sourceModifiedAt = std::to_string(file.modifiedAt.time_since_epoch().count());
            media.origin = origin;
            media.folderImportId = folderImportId;
            const auto mediaId = repository_.insertMedia(media);
            const auto stored = repository_.getMedia(mediaId);
            const bool titleOnlyFolderMedia = origin == "folder_import" && !parsed.episode &&
                parsed.title.size() <= 200 &&
                std::any_of(parsed.title.begin(), parsed.title.end(), [](unsigned char ch) {
                    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch >= 0x80;
                });
            // 无集数的命名文件仍可刮削番剧；集数保持空白，不自动猜测或整理。
            if (stored && stored->title.empty() && !parsed.title.empty() &&
                (parsed.episode || titleOnlyFolderMedia)) {
                repository_.updateMediaCorrection(mediaId,
                    MediaCorrection{parsed.title,
                        parsed.season ? std::to_string(*parsed.season) : "",
                        media.episodeNumber, media.episodeType, media.confidence, std::nullopt},
                    MediaCorrectionIntent::automaticScan);
            }
            ++scan.processedCount;
        }
        // Reconcile against every observed video, including files not yet stable enough to import.
        std::vector<std::string> observedPaths;
        for (const auto& path : scanner.observedPaths()) observedPaths.push_back(utf8(path));
        repository_.markMissingMedia(origin, observedPaths, folderImportId);
        scan.status = "completed";
    } catch (const std::exception& error) {
        scan.status = "failed";
        scan.errorCount = 1;
        scan.errorSummary = error.what();
    }
    repository_.updateScan(scan);
    return scan;
}

ScanRecord MediaService::getScan(std::int64_t id) const {
    const std::lock_guard lock(mutex_);
    if (id <= 0) throw ApiError(400, "invalid_id", "scan id must be positive");
    auto found = repository_.getScan(id);
    if (!found) throw ApiError(404, "scan_not_found", "scan not found");
    return *found;
}

std::vector<MediaRecord> MediaService::listInbox() const {
    const std::lock_guard lock(mutex_);
    return repository_.listInbox();
}

InboxPage MediaService::listInboxPage(std::int64_t offset, int limit,
                                      std::optional<std::string> origin) const {
    const std::lock_guard lock(mutex_);
    return repository_.listInboxPage(offset, limit, std::move(origin));
}

MediaRecord MediaService::correct(std::int64_t id, const MediaCorrection& correction) {
    const std::lock_guard lock(mutex_);
    if (id <= 0) throw ApiError(400, "invalid_id", "media id must be positive");
    if (!repository_.getMedia(id)) throw ApiError(404, "media_not_found", "media not found");
    if (correction.title.empty() || correction.title.size() > 200 ||
        correction.season.size() > 40 || !EpisodeNumber::parse(correction.episodeNumber) ||
        correction.confidence < 0 || correction.confidence > 1 ||
        (correction.bangumiSubjectId && *correction.bangumiSubjectId <= 0)) {
        throw ApiError(400, "invalid_correction", "invalid title, season, episode or binding");
    }
    parseType(correction.episodeType);
    // Planner validates title as a safe path component before persisting it.
    try {
        SourceFile dummy{fs::path("source.mkv"), 0, {}};
        const auto validation = planner_.preview(dummy, correction.title,
            parseType(correction.episodeType), *EpisodeNumber::parse(correction.episodeNumber));
        (void)validation;
    } catch (const std::invalid_argument&) {
        throw ApiError(400, "invalid_title", "invalid media title");
    }
    repository_.updateMediaCorrection(id, correction);
    return *repository_.getMedia(id);
}

PreviewResponse MediaService::preview(std::int64_t id, const std::string& operation) {
    const std::lock_guard lock(mutex_);
    if (operation != "hardlink" && operation != "copy" && operation != "symlink")
        throw ApiError(400, "invalid_operation", "operation must be hardlink, copy or symlink");
    if (id <= 0) throw ApiError(400, "invalid_id", "media id must be positive");
    const auto media = repository_.getMedia(id);
    if (!media) throw ApiError(404, "media_not_found", "media not found");
    const auto episodeType = parseType(media->episodeType);
    const auto episode = EpisodeNumber::parse(media->episodeNumber);
    auto targetTitle = media->title;
    if (media->animeId) {
        const auto anime = repository_.getAnime(*media->animeId);
        if (!anime) throw ApiError(404, "anime_not_found", "linked anime not found");
        targetTitle = anime->displayTitle;
    }
    if (!episode || targetTitle.empty())
        throw ApiError(409, "parse_incomplete", "correct title and episode before preview");
    const auto* sourceBytes = reinterpret_cast<const char8_t*>(media->sourcePath.data());
    const fs::path source(std::u8string_view(sourceBytes, media->sourcePath.size()));
    if (!fs::is_regular_file(source) || fs::is_symlink(source))
        throw ApiError(409, "source_changed", "source missing or replaced");
    const auto canonical = fs::canonical(source);
    fs::path expectedRoot = media->origin == "external_import" ? importRoot_ : sourceRoot_;
    if (media->origin == "folder_import") {
        const auto folder = media->folderImportId
            ? repository_.getFolderImport(*media->folderImportId) : std::nullopt;
        if (!folder) throw ApiError(409, "folder_import_not_found", "folder import is unavailable");
        expectedRoot = fromUtf8(folder->rootPath);
        std::error_code error;
        if (!fs::is_directory(expectedRoot, error) || error ||
            fs::canonical(expectedRoot, error) != expectedRoot || error)
            throw ApiError(409, "folder_unavailable", "folder import root changed");
    }
    if (!within(fs::weakly_canonical(expectedRoot), canonical) || canonical != source)
        throw ApiError(409, "source_outside_root", "source is outside configured root");
    const auto size = fs::file_size(source);
    const auto modified = fs::last_write_time(source).time_since_epoch().count();
    if (size != static_cast<std::uintmax_t>(media->sizeBytes) ||
        media->sourceModifiedAt != std::to_string(modified))
        throw ApiError(409, "source_changed", "source snapshot changed");
    TargetPlan target;
    try {
        target = planner_.preview(SourceFile{source, size, fs::last_write_time(source)},
                                  targetTitle, episodeType, *episode);
    } catch (const std::invalid_argument& error) {
        throw ApiError(400, "invalid_target", error.what());
    }
    const auto expiresAt = expiration();
    const auto state = target.conflicts.empty() ? "pending" : "conflict:" + target.conflicts.front();
    const auto key = std::to_string(id) + ":" + std::to_string(size) + ":" +
                     std::to_string(modified) + ":" + utf8(target.target) + ":" + operation +
                     ":" + state + ":" + expiresAt;
    PlanRecord plan{};
    plan.mediaFileId = id;
    plan.sourcePath = utf8(source);
    plan.sourceSize = static_cast<std::int64_t>(size);
    plan.sourceModifiedAt = std::to_string(modified);
    plan.targetPath = utf8(target.target);
    plan.operation = operation;
    plan.expiresAt = expiresAt;
    plan.executionState = state;
    plan.idempotencyKey = key;
    plan.id = repository_.insertPlan(plan);
    return {plan.id, plan.targetPath, plan.operation, target.conflicts, plan.expiresAt};
}
} // namespace anime_vault::api
