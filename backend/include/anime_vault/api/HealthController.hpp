#pragma once

#include <charconv>
#include <algorithm>
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
    std::string_view instanceToken;
};

inline constexpr HealthPayload makeHealthPayload(std::string_view instanceToken = {}) noexcept {
    return {"ok", "anime-vault", instanceToken};
}

inline std::string serializeHealthPayload(const HealthPayload& payload) {
    // The optional token is validated as lowercase hex before it enters this JSON.
    auto json = "{\"status\":\"" + std::string(payload.status) +
        "\",\"service\":\"" + std::string(payload.service) + "\"";
    if (!payload.instanceToken.empty())
        json += ",\"instanceToken\":\"" + std::string(payload.instanceToken) + "\"";
    return json + "}";
}

inline std::string resolveInstanceToken(const char* value) {
    if (!value) return {};
    const std::string_view token(value);
    if (token.size() != 32 || !std::all_of(token.begin(), token.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    })) throw std::invalid_argument("ANIME_VAULT_INSTANCE_TOKEN must be 32 lowercase hexadecimal characters");
    return std::string(token);
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

void registerHealthEndpoint(std::string instanceToken = {});

}  // namespace anime_vault::api
