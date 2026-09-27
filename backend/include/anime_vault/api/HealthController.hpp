#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace anime_vault::api {

// Local development default when ANIME_VAULT_PORT is unset.
inline constexpr std::uint16_t kDefaultHealthPort = 8848;
inline constexpr std::string_view kHealthBindAddress = "127.0.0.1";

struct HealthPayload {
    std::string_view status;
    std::string_view service;
};

inline constexpr HealthPayload makeHealthPayload() noexcept {
    return {"ok", "anime-vault"};
}

inline std::string serializeHealthPayload(const HealthPayload& payload) {
    // Both values are fixed by makeHealthPayload, so no input can enter this JSON.
    return "{\"status\":\"" + std::string(payload.status) +
           "\",\"service\":\"" + std::string(payload.service) + "\"}";
}

inline std::optional<std::uint16_t> parseHealthPort(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    unsigned int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value == 0 || value > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

inline std::uint16_t resolveHealthPort(const char* environmentValue) {
    if (environmentValue == nullptr) {
        return kDefaultHealthPort;
    }
    const auto port = parseHealthPort(environmentValue);
    if (!port) {
        throw std::invalid_argument("ANIME_VAULT_PORT must be a decimal integer from 1 to 65535");
    }
    return *port;
}

void registerHealthEndpoint();

}  // namespace anime_vault::api
