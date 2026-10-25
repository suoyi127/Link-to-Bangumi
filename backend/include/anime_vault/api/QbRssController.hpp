#pragma once
#include "anime_vault/infrastructure/network/QbConnectionManager.hpp"
#include <filesystem>
#include <functional>

namespace anime_vault::api {
void registerQbRssEndpoints(std::shared_ptr<QbConnectionManager> manager, std::filesystem::path source,
    std::function<std::string()> downloadError, std::function<void()> validateSourceReady);
}
