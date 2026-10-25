#pragma once

#include "anime_vault/ports/ScanRepository.hpp"
#include "anime_vault/ports/OrganizationRepository.hpp"
#include "anime_vault/ports/SettingsRepository.hpp"
#include "anime_vault/ports/MetadataCache.hpp"
#include "anime_vault/ports/AnimeRepository.hpp"

namespace anime_vault {

// 兼容组合入口；用途端口各自唯一拥有持久化操作。
class MediaRepository : public ScanRepository, public OrganizationRepository, public SettingsRepository, public MetadataCache, public AnimeRepository {
public:
    ~MediaRepository() override = default;
};

} // namespace anime_vault
