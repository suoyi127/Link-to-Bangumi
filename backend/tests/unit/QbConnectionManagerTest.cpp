#include "anime_vault/infrastructure/network/QbConnectionManager.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;
using anime_vault::QbConnectionConfig;
using anime_vault::QbConnectionManager;

TEST_CASE("saved qB connection replaces environment fallback without exposing password") {
    const auto data = fs::temp_directory_path() /
        ("anime-vault-qb-config-" + std::to_string(std::random_device{}()));
    QbConnectionManager manager(data, {"http://[::1]:8080", "environment", "environment-secret"});
    struct Cleanup { QbConnectionManager& manager; ~Cleanup() { manager.clear(); } } cleanup{manager};
    REQUIRE(manager.summary().source == "environment");
    REQUIRE(manager.summary().username == "environment");
    auto before = manager.current();
    manager.save({"http://127.0.0.1:8090", "saved-user", "saved-secret"});
    REQUIRE(manager.summary().source == "saved");
    REQUIRE(manager.summary().url == "http://127.0.0.1:8090");
    REQUIRE(manager.summary().username == "saved-user");
    REQUIRE(manager.current() != before);
    REQUIRE(manager.current()->baseUrl() == "http://127.0.0.1:8090");
    QbConnectionManager reopened(data, {"http://[::1]:8080", "environment", "environment-secret"});
    REQUIRE(reopened.summary().source == "saved");
    REQUIRE(reopened.draft({"http://127.0.0.1:8091", "saved-user", ""}).password == "saved-secret");
    manager.clear();
    REQUIRE(manager.summary().source == "environment");
    REQUIRE(manager.current()->baseUrl() == "http://[::1]:8080");
}

TEST_CASE("new qB connection requires a password and rejects unsafe input") {
    const auto data = fs::temp_directory_path() /
        ("anime-vault-qb-config-" + std::to_string(std::random_device{}()));
    QbConnectionManager manager(data, {"http://[::1]:8080", "", ""});
    REQUIRE(manager.summary().source == "none");
    REQUIRE_FALSE(manager.summary().configured);
    REQUIRE_THROWS(manager.draft({"http://[::1]:8080", "user", ""}));
    REQUIRE_THROWS(manager.draft({"http://localhost:8080", "user", "secret"}));
    REQUIRE_THROWS(manager.draft({"http://[::1]:8080", "bad\nuser", "secret"}));
}
