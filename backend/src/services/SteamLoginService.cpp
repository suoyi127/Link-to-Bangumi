#include "anime_vault/services/SteamLoginService.hpp"
#include "anime_vault/infrastructure/network/SteamClient.hpp"
#include "anime_vault/domain/Game.hpp"
#include <array>
#include <random>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif
namespace anime_vault {
namespace {
std::string randomToken() {
    std::array<unsigned char,32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
        throw GameError("steam_login_failed");
#else
    std::random_device random;
    for (auto& b : bytes) b=static_cast<unsigned char>(random());
#endif
    std::string out; constexpr char hex[]="0123456789abcdef";
    for (auto b : bytes) { out+=hex[b>>4]; out+=hex[b&15]; }
    return out;
}
std::string utf8(const std::filesystem::path& path) {
    const auto s=path.u8string(); return {s.begin(),s.end()};
}
}
SteamLoginService::SteamLoginService(ProcessLauncher& launcher,std::filesystem::path home,std::filesystem::path profile,
    Save save,std::function<bool()> busy,Clock clock)
    :launcher_(launcher),home_(std::filesystem::absolute(home)),profile_(std::filesystem::absolute(profile)),
    save_(std::move(save)),busy_(std::move(busy)),clock_(std::move(clock)) {}
void SteamLoginService::expire() {
    if (state_=="pending" && clock_()>=deadline_) { state_="expired"; token_.clear(); }
}
SteamLoginStart SteamLoginService::start(unsigned short port) {
    std::lock_guard lock(mutex_); expire();
    if (!port) throw GameError("invalid_request");
    if (state_=="pending") throw GameError("steam_login_in_progress");
    if (busy_()) throw GameError("steam_import_in_progress");
    std::filesystem::path helper;
    // 只从安装目录或本项目的固定构建路径启动，不接受网页指定可执行文件。
    for (const auto& candidate : {home_/"AnimeVault.exe",
        home_/"desktop/AnimeVault.Desktop/bin/Debug/net10.0-windows/AnimeVault.exe",
        home_/"desktop/AnimeVault.Desktop/bin/Release/net10.0-windows/AnimeVault.exe"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate,error)) { helper=candidate; break; }
    }
    if (helper.empty()) throw GameError("steam_login_helper_unavailable");
    sessionId_=randomToken(); token_=randomToken(); state_="pending"; error_.clear();
    deadline_=clock_()+std::chrono::minutes(15);
    try {
        if (!launcher_.launch(helper,{"--steam-login","--callback",
            "http://127.0.0.1:"+std::to_string(port)+"/api/steam/login/complete","--token",token_,"--profile",utf8(profile_)}))
            throw GameError("steam_login_failed");
    } catch (...) { state_="failed"; error_="steam_login_failed"; token_.clear(); throw GameError(error_); }
    return {sessionId_,state_};
}
SteamLoginStatus SteamLoginService::status(const std::string& sessionId) {
    std::lock_guard lock(mutex_); expire();
    if (sessionId.empty() || sessionId!=sessionId_) throw GameError("steam_login_not_found");
    return {state_,error_};
}
void SteamLoginService::complete(const std::string& token,const std::string& steamId,const std::string& apiKey,bool cancelled) {
    std::lock_guard lock(mutex_); expire();
    // 状态查询标识和回传凭证分离；凭证只可使用一次，不从 GET 接口返回。
    if (state_!="pending" || token.empty() || token!=token_) throw GameError("steam_login_invalid_token");
    if (cancelled) { state_="cancelled"; token_.clear(); return; }
    if (steamId.empty() || apiKey.empty()) throw GameError("invalid_steam_config");
    SteamConfiguration::validate({"",steamId,apiKey});
    if (busy_()) throw GameError("steam_import_in_progress");
    save_(steamId,apiKey);
    state_="completed"; token_.clear();
}
}
