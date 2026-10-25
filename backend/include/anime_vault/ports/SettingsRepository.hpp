#pragma once

#include "anime_vault/domain/MediaRecords.hpp"

namespace anime_vault {

class SettingsRepository {
public:
    virtual ~SettingsRepository() = default;
    virtual AuditPage listAuditPage(std::int64_t offset, int limit,
                                    std::optional<std::int64_t> animeId = std::nullopt) const = 0;
    virtual UiPreferences getUiPreferences() const = 0;
    virtual void putUiPreferences(const UiPreferences& preferences) = 0;
};

} // namespace anime_vault
