#include "anime_vault/services/CoverScraper.hpp"
#include "anime_vault/repositories/MediaRepository.hpp"

#include <fstream>
#include <iomanip>
#include <random>
#include <regex>
#include <sstream>

namespace anime_vault {
namespace {
constexpr std::size_t kMaxCoverBytes = 5 * 1024 * 1024;
std::string token() {
    std::random_device random;
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(8) << random()
           << std::setw(8) << random();
    return output.str();
}
std::filesystem::path coverFile(const std::filesystem::path& root, std::int64_t animeId,
                                std::int64_t subjectId, std::string_view filename) {
    return root / "covers" / (std::to_string(animeId) + "-" + std::to_string(subjectId) + "-" +
                               std::string(filename));
}
}

CoverScraper::CoverScraper(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                           CoverImageFetcher& fetcher, std::filesystem::path dataRoot)
    : repository_(repository), bangumi_(std::move(bangumi)), fetcher_(fetcher),
      dataRoot_(std::move(dataRoot)) {}

std::optional<std::string> CoverScraper::allowedImagePath(std::string_view url) {
    constexpr std::string_view prefix = "https://lain.bgm.tv";
    if (!url.starts_with(prefix) || url.size() > 2048) return std::nullopt;
    const auto path = url.substr(prefix.size());
    static const std::regex allowed(
        R"(^/(?:r/[0-9]{1,4}/)?pic/cover/[A-Za-z0-9/_-]+\.(?:jpe?g|png|webp)(?:\?[A-Za-z0-9._%=&-]{1,200})?$)",
        std::regex::icase);
    if (!std::regex_match(path.begin(), path.end(), allowed)) return std::nullopt;
    return std::string(path);
}

std::optional<CachedCover> CoverScraper::detectImage(std::string_view bytes) {
    if (bytes.size() < 4 || bytes.size() > kMaxCoverBytes) return std::nullopt;
    if (static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 &&
        static_cast<unsigned char>(bytes[2]) == 0xff)
        return CachedCover{{}, "image/jpeg", "jpg"};
    if (bytes.size() >= 8 && bytes.substr(0, 8) == "\x89PNG\r\n\x1a\n")
        return CachedCover{{}, "image/png", "png"};
    if (bytes.size() >= 12 && bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 4) == "WEBP")
        return CachedCover{{}, "image/webp", "webp"};
    return std::nullopt;
}

void CoverScraper::refresh(std::int64_t animeId, Completion completion) {
    std::optional<AnimeRecord> anime;
    try { anime = repository_.getAnime(animeId); }
    catch (...) { completion({"", "cover_storage_error"}); return; }
    if (!anime) { completion({"", "anime_not_found"}); return; }
    if (!anime->bangumiSubjectId) { completion({"", "cover_subject_unbound"}); return; }
    const auto expectedSubject = *anime->bangumiSubjectId;
    auto self = shared_from_this();
    bangumi_->subject(expectedSubject, [self, animeId, expectedSubject,
                                        completion = std::move(completion)](BangumiSubjectResult result) mutable {
        if (!result.subject || !result.errorCode.empty()) {
            completion({"", result.errorCode.empty() ? "cover_unavailable" : result.errorCode});
            return;
        }
        self->cacheSubject(animeId, std::move(*result.subject), std::move(completion));
    });
}

void CoverScraper::cacheSubject(std::int64_t animeId, BangumiSubject subject, Completion completion) {
    if (!completion) completion = [](CoverScrapeResult) {};
    if (animeId <= 0 || subject.id <= 0 || !allowedImagePath(subject.coverUrl)) {
        completion({"", "cover_unavailable"}); return;
    }
    auto self = shared_from_this();
    fetcher_.fetch(subject.coverUrl, [self, animeId, subjectId = subject.id,
                                      completion = std::move(completion)](
        std::optional<std::string> bytes, std::string) mutable {
        if (!bytes) { completion({"", "cover_unavailable"}); return; }
        const auto format = detectImage(*bytes);
        if (!format) { completion({"", "cover_invalid_image"}); return; }
        std::filesystem::path finalPath, temporaryPath;
        CoverScrapeResult outcome;
        try {
            // Publish a new immutable cache file before changing the DB URL. A rebind
            // race cannot make the old subject's file visible under the new binding.
            std::filesystem::create_directories(self->dataRoot_ / "covers");
            if (std::filesystem::canonical(self->dataRoot_ / "covers").parent_path() !=
                std::filesystem::canonical(self->dataRoot_))
                throw std::runtime_error("cover cache directory escaped data root");
            const auto filename = token() + "." + format->extension;
            finalPath = coverFile(self->dataRoot_, animeId, subjectId, filename);
            temporaryPath = finalPath;
            temporaryPath += ".tmp";
            {
                std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
                if (!output.write(bytes->data(), static_cast<std::streamsize>(bytes->size())))
                    throw std::runtime_error("cover cache write failed");
            }
            std::filesystem::rename(temporaryPath, finalPath);
            const auto url = "/api/covers/" + std::to_string(animeId) + "/" +
                std::to_string(subjectId) + "/" + filename;
            if (!self->repository_.updateCoverIfBound(animeId, subjectId, url)) {
                std::error_code ignored;
                std::filesystem::remove(finalPath, ignored);
                outcome.errorCode = "cover_binding_changed";
            } else outcome.coverUrl = url;
        } catch (...) {
            std::error_code ignored;
            if (!temporaryPath.empty()) std::filesystem::remove(temporaryPath, ignored);
            if (!finalPath.empty()) std::filesystem::remove(finalPath, ignored);
            outcome.errorCode = "cover_storage_error";
        }
        completion(std::move(outcome));
    });
}

std::optional<CachedCover> CoverScraper::readCached(std::int64_t animeId, std::int64_t subjectId,
                                                     std::string_view filename) const {
    if (animeId <= 0 || subjectId <= 0) return std::nullopt;
    static const std::regex safeName(R"(^[0-9a-f]{16}\.(?:jpg|png|webp)$)");
    if (!std::regex_match(filename.begin(), filename.end(), safeName)) return std::nullopt;
    const auto expected = "/api/covers/" + std::to_string(animeId) + "/" +
        std::to_string(subjectId) + "/" + std::string(filename);
    try {
        const auto anime = repository_.getAnime(animeId);
        if (!anime || anime->bangumiSubjectId != subjectId || anime->coverUrl != expected)
            return std::nullopt;
        const auto path = coverFile(dataRoot_, animeId, subjectId, filename);
        if (std::filesystem::canonical(path).parent_path() !=
            std::filesystem::canonical(dataRoot_ / "covers")) return std::nullopt;
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > kMaxCoverBytes)
            return std::nullopt;
        std::ifstream input(path, std::ios::binary);
        std::string bytes(std::istreambuf_iterator<char>{input}, {});
        auto image = detectImage(bytes);
        if (!image || filename.substr(filename.find_last_of('.') + 1) != image->extension)
            return std::nullopt;
        image->bytes = std::move(bytes);
        return image;
    } catch (...) { return std::nullopt; }
}
}
