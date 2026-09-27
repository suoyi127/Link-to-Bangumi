#include "anime_vault/api/HealthController.hpp"

#include <stdexcept>
#include <string_view>

int main() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    const auto payload = anime_vault::api::makeHealthPayload();
    require(payload.status == "ok", "health status must be ok");
    require(payload.service == "anime-vault", "health service must be anime-vault");
    require(anime_vault::api::serializeHealthPayload(payload) ==
                R"({"status":"ok","service":"anime-vault"})",
            "health JSON must have exactly the two public fields");
    constexpr std::string_view instanceToken = "0123456789abcdef0123456789abcdef";
    require(anime_vault::api::serializeHealthPayload(
                anime_vault::api::makeHealthPayload(instanceToken)) ==
                R"({"status":"ok","service":"anime-vault","instanceToken":"0123456789abcdef0123456789abcdef"})",
            "desktop health must echo only its validated instance token");
    require(anime_vault::api::resolveInstanceToken(nullptr).empty(),
            "developer health must not have an instance token");
    require(anime_vault::api::resolveInstanceToken(instanceToken.data()) == instanceToken,
            "valid desktop token must be accepted");
    bool rejectedToken = false;
    try { static_cast<void>(anime_vault::api::resolveInstanceToken("\"injected\"")); }
    catch (const std::invalid_argument&) { rejectedToken = true; }
    require(rejectedToken, "malformed instance token must fail startup");

    require(anime_vault::api::kHealthBindAddress == "127.0.0.1",
            "server must bind to loopback only");
    require(anime_vault::api::kDefaultHealthPort == 8848,
            "existing local port must remain the default");
    require(anime_vault::api::parseHealthPort("1") == 1, "minimum port must work");
    require(anime_vault::api::parseHealthPort("8848") == 8848, "configured port must work");
    require(anime_vault::api::parseHealthPort("65535") == 65535, "maximum port must work");
    for (const std::string_view invalid : {"", "0", "65536", "-1", "+1",
                                           " 8088", "8088 ", "80x", "999999999999999999"}) {
        require(!anime_vault::api::parseHealthPort(invalid).has_value(),
                "invalid port must be rejected");
    }
    require(anime_vault::api::resolveHealthPort(nullptr) ==
                anime_vault::api::kDefaultHealthPort,
            "unset port must use the documented default");
    bool rejectedEmptyPort = false;
    try {
        static_cast<void>(anime_vault::api::resolveHealthPort(""));
    } catch (const std::invalid_argument&) {
        rejectedEmptyPort = true;
    }
    require(rejectedEmptyPort, "explicitly empty port must fail startup");
}
