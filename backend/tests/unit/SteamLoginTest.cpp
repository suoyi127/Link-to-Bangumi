#include "anime_vault/services/SteamLoginService.hpp"
#include "anime_vault/domain/Game.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <random>
using namespace anime_vault;
namespace fs = std::filesystem;
TEST_CASE("Steam login tokens expire, cannot replay and do not appear in status") {
    const auto root = fs::temp_directory_path() / ("av-login-" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code e; fs::remove_all(root,e); } } cleanup{root};
    std::ofstream(root / "AnimeVault.exe") << "fixture";
    struct Launcher : ProcessLauncher {
        std::vector<std::string> args;
        bool launch(const fs::path&, const std::vector<std::string>& a) override { args=a; return true; }
    } launcher;
    int saves=0; bool busy=false;
    auto now=std::chrono::steady_clock::now();
    SteamLoginService service(launcher,root,root/"Profile",[&](auto id,auto key) {
        REQUIRE(id=="76561198000000000"); REQUIRE(key==std::string(32,'a')); ++saves;
    },[&]{return busy;},[&]{return now;});
    const auto started=service.start(8848);
    REQUIRE(started.state=="pending");
    REQUIRE(launcher.args[2]=="http://127.0.0.1:8848/api/steam/login/complete");
    const auto token=launcher.args[4];
    REQUIRE(token.size()==64); REQUIRE(started.sessionId!=token);
    REQUIRE_THROWS_AS(service.complete(started.sessionId,"76561198000000000",std::string(32,'a'),false),GameError);
    REQUIRE_THROWS_AS(service.start(8848),GameError);
    REQUIRE_THROWS_AS(service.complete(token,"bad",std::string(32,'a'),false),GameError);
    busy=true;
    REQUIRE_THROWS_AS(service.complete(token,"76561198000000000",std::string(32,'a'),false),GameError);
    busy=false;
    service.complete(token,"76561198000000000",std::string(32,'a'),false);
    REQUIRE(saves==1); REQUIRE(service.status(started.sessionId).state=="completed");
    REQUIRE_THROWS_AS(service.complete(token,"76561198000000000",std::string(32,'a'),false),GameError);
    const auto next=service.start(8848);
    now+=std::chrono::minutes(16);
    REQUIRE(service.status(next.sessionId).state=="expired");
    REQUIRE_THROWS_AS(service.complete(launcher.args[4],"76561198000000000",std::string(32,'a'),false),GameError);
    const auto cancelled=service.start(8848);
    service.complete(launcher.args[4],"","",true);
    REQUIRE(service.status(cancelled.sessionId).state=="cancelled");
    REQUIRE(saves==1);
}
