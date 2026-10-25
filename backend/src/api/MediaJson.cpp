#include "anime_vault/api/MediaJson.hpp"
namespace anime_vault::api {
Json::Value scanJson(const ScanRecord& scan) {
    Json::Value json;
    json["id"] = Json::Int64(scan.id);
    json["source"] = scan.source;
    json["status"] = scan.status;
    json["discoveredCount"] = Json::Int64(scan.discoveredCount);
    json["processedCount"] = Json::Int64(scan.processedCount);
    json["errorCount"] = Json::Int64(scan.errorCount);
    // Scanner exceptions may contain private paths; never return their raw text.
    json["errorSummary"] = scan.errorSummary.empty() ? "" : "scan_failed";
    return json;
}
Json::Value folderImportJson(const FolderImportRecord& folder) {
    Json::Value json;
    json["id"] = Json::Int64(folder.id);
    json["path"] = folder.rootPath;
    return json;
}
Json::Value mediaJson(const MediaRecord& media) {
    Json::Value json;
    json["id"] = Json::Int64(media.id);
    json["scanId"] = Json::Int64(media.scanId);
    json["sourcePath"] = media.sourcePath;
    json["filename"] = media.filename;
    json["parsedTitle"] = media.parsedTitle;
    json["title"] = media.title;
    json["season"] = media.season;
    json["episodeNumber"] = media.episodeNumber;
    json["rawEpisodeNumber"] = media.rawEpisodeNumber;
    json["episodeNumberSource"] = media.episodeNumberSource;
    json["episodeType"] = media.episodeType;
    json["sizeBytes"] = Json::Int64(media.sizeBytes);
    json["status"] = media.status;
    json["origin"] = media.origin;
    if (media.animeId) json["animeId"] = Json::Int64(*media.animeId);
    else json["animeId"] = Json::nullValue;
    json["confidence"] = media.confidence;
    if (media.bangumiSubjectId) json["bangumiSubjectId"] = Json::Int64(*media.bangumiSubjectId);
    return json;
}
Json::Value animeJson(const AnimeRecord& anime, bool detail) {
    Json::Value json;
    json["id"] = Json::Int64(anime.id);
    json["displayTitle"] = anime.displayTitle;
    json["originalTitle"] = anime.originalTitle;
    json["season"] = anime.season;
    if (anime.year) json["year"] = *anime.year;
    if (anime.bangumiSubjectId) json["bangumiSubjectId"] = Json::Int64(*anime.bangumiSubjectId);
    json["coverUrl"] = anime.coverUrl;
    json["locked"] = anime.locked;
    json["importedMonth"] = anime.importedMonth;
    if (detail) {
        json["aliases"] = Json::Value(Json::arrayValue);
        for (const auto& alias : anime.aliases) json["aliases"].append(alias);
        json["media"] = Json::Value(Json::arrayValue);
        for (const auto& media : anime.media) json["media"].append(mediaJson(media));
        if (anime.nextMediaOffset) json["nextMediaOffset"] = Json::Int64(*anime.nextMediaOffset);
        else json["nextMediaOffset"] = Json::nullValue;
    }
    return json;
}
Json::Value candidateJson(const BangumiCandidate& candidate) {
    Json::Value json;
    json["id"] = Json::Int64(candidate.id);
    json["name"] = candidate.name;
    json["nameCn"] = candidate.nameCn;
    json["date"] = candidate.date;
    json["coverUrl"] = candidate.coverUrl;
    json["episodeCount"] = candidate.episodeCount;
    json["type"] = candidate.type;
    json["score"] = candidate.score;
    return json;
}
Json::Value previewJson(const PreviewResponse& preview) {
    Json::Value json;
    json["id"] = Json::Int64(preview.id);
    json["targetPath"] = preview.targetPath;
    json["operation"] = preview.operation;
    json["expiresAt"] = preview.expiresAt;
    Json::Value conflicts(Json::arrayValue);
    for (const auto& conflict : preview.conflicts) conflicts.append(conflict);
    json["conflicts"] = conflicts;
    return json;
}
}
