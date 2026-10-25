#pragma once
#include <json/json.h>
#include "anime_vault/repositories/MediaRepository.hpp"
#include "anime_vault/services/BangumiService.hpp"
#include "anime_vault/api/MediaService.hpp"
namespace anime_vault::api {
Json::Value scanJson(const ScanRecord& scan);
Json::Value folderImportJson(const FolderImportRecord& folder);
Json::Value mediaJson(const MediaRecord& media);
Json::Value animeJson(const AnimeRecord& anime, bool detail);
Json::Value candidateJson(const BangumiCandidate& candidate);
Json::Value previewJson(const PreviewResponse& preview);
}
