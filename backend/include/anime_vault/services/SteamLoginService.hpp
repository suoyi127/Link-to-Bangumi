#pragma once
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <chrono>
#include <functional>
#include <mutex>
namespace anime_vault {
struct SteamLoginStart { std::string sessionId, state; };
struct SteamLoginStatus { std::string state, errorCode; };
class SteamLoginService {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    using Save = std::function<void(std::string,std::string)>;
    SteamLoginService(ProcessLauncher& launcher, std::filesystem::path home, std::filesystem::path profile,
        Save save, std::function<bool()> busy, Clock clock = []{return std::chrono::steady_clock::now();});
    SteamLoginStart start(unsigned short port);
    SteamLoginStatus status(const std::string& sessionId);
    void complete(const std::string& token,const std::string& steamId,const std::string& apiKey,bool cancelled);
private:
    void expire();
    ProcessLauncher& launcher_;
    std::filesystem::path home_,profile_;
    Save save_;
    std::function<bool()> busy_;
    Clock clock_;
    std::mutex mutex_;
    std::string sessionId_,token_,state_,error_;
    std::chrono::steady_clock::time_point deadline_;
};
}
