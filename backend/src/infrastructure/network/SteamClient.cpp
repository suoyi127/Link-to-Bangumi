#include "anime_vault/infrastructure/network/SteamClient.hpp"
#include "anime_vault/domain/Game.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cwctype>
#include <deque>
#include <limits>
#include <memory>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace anime_vault {
namespace {
using Json = nlohmann::json;
bool decimal(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(),s.end(),[](char c) { return c >= '0' && c <= '9'; });
}
std::optional<std::int64_t> number(const Json& value) {
    if (value.is_number_unsigned()) {
        const auto n = value.get<std::uint64_t>();
        if (n <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
            return static_cast<std::int64_t>(n);
    } else if (value.is_number_integer()) {
        const auto n = value.get<std::int64_t>();
        if (n >= 0) return n;
    }
    return std::nullopt;
}
const Json& member(const Json& value, const char* name) {
    static const Json missing;
    if (!value.is_object()) return missing;
    const auto it = value.find(name);
    return it == value.end() ? missing : *it;
}
Json parse(const std::string& text) { return Json::parse(text, nullptr, false); }
struct Fetch : std::enable_shared_from_this<Fetch> {
    SteamClient::Request request;
    SteamClient::Completion completion;
    SteamConfig config;
    std::map<std::string,SteamStats> result;
    std::vector<std::string> ids;
    std::size_t position{};
    bool finished{};
    std::mutex mutex;
    bool pumping{};
    std::deque<std::function<void()>> queue;

    // 同步测试传输与异步传输共用队列，避免每个游戏递归增加调用栈。
    void enqueue(std::function<void()> action) {
        std::unique_lock lock(mutex);
        queue.push_back(std::move(action));
        if (pumping) return;
        pumping = true;
        while (!queue.empty()) {
            auto next = std::move(queue.front()); queue.pop_front();
            lock.unlock(); next(); lock.lock();
        }
        pumping = false;
    }
    void done(const std::string& error = {}) {
        if (finished) return;
        finished = true;
        for (auto& [id,stats] : result) {
            if (!error.empty() && (!stats.playtimeMinutes || !stats.achievementsUnlocked || !stats.achievementsTotal))
                stats.errorCode = error;
            else if (stats.errorCode.empty() && (!stats.playtimeMinutes || !stats.achievementsUnlocked || !stats.achievementsTotal))
                stats.errorCode = "steam_stats_unavailable";
        }
        auto callback = std::move(completion);
        callback(std::move(result));
    }
    void send(std::string path, std::function<void(int,const Json&)> consume, bool stopOnServerError = false) {
        auto self = shared_from_this();
        auto callback = [self,consume = std::move(consume),stopOnServerError](int status,std::string body) {
            self->enqueue([self,status,body = std::move(body),consume,stopOnServerError] {
                if (self->finished) return;
                // 单个游戏可能返回 500，仍需查询架构并继续；限流、离线和账号级故障才终止批次。
                if (status == 429 || status <= 0 || (stopOnServerError && status >= 500)) {
                    self->done(status == 429 ? "steam_rate_limited" : "steam_stats_unavailable");
                    return;
                }
                consume(status, parse(body));
            });
        };
        try { request(std::move(path), callback); }
        catch (...) { callback(0, {}); }
    }
    std::string auth() const { return "key=" + config.apiKey + "&steamid=" + config.steamId; }
    void start() {
        auto self = shared_from_this();
        send("/IPlayerService/GetOwnedGames/v1/?" + auth() + "&include_appinfo=false&include_played_free_games=true",
            [self](int status,const Json& value) {
                if (status == 401 || status == 403) { self->done("steam_stats_unavailable"); return; }
                const auto& games = member(member(value,"response"),"games");
                if (status == 200 && games.is_array()) {
                    for (const auto& game : games) {
                        const auto id = number(member(game,"appid"));
                        const auto minutes = number(member(game,"playtime_forever"));
                        if (!id || !minutes) continue;
                        const auto found = self->result.find(std::to_string(*id));
                        if (found != self->result.end()) found->second.playtimeMinutes = minutes;
                    }
                }
                self->next();
            }, true);
    }
    void next() {
        if (position == ids.size()) { done(); return; }
        const auto id = ids[position++];
        auto self = shared_from_this();
        send("/ISteamUserStats/GetPlayerAchievements/v1/?" + auth() + "&appid=" + id,
            [self,id](int status,const Json& value) {
                const auto& player = member(value,"playerstats");
                const auto& achievements = member(player,"achievements");
                const auto& success = member(player,"success");
                if (status == 400 && success.is_boolean() && !success.get<bool>() &&
                    member(player,"error") == "Requested app has no stats")
                    self->result[id].errorCode = "steam_stats_not_supported";
                bool valid = status == 200 && success.is_boolean() && success.get<bool>() && achievements.is_array();
                std::int64_t unlocked = 0;
                if (valid) for (const auto& achievement : achievements) {
                    const auto achieved = number(member(achievement,"achieved"));
                    if (!achieved || *achieved > 1) { valid = false; break; }
                    unlocked += *achieved;
                }
                if (valid) {
                    self->result[id].achievementsUnlocked = unlocked;
                    if (!achievements.empty()) {
                        self->result[id].achievementsTotal = static_cast<std::int64_t>(achievements.size());
                        self->next(); return;
                    }
                }
                self->schema(id);
            });
    }
    void schema(const std::string& id) {
        auto self = shared_from_this();
        send("/ISteamUserStats/GetSchemaForGame/v2/?key=" + config.apiKey + "&appid=" + id,
            [self,id](int status,const Json& value) {
                const auto& game = member(value,"game");
                const auto& stats = member(game,"availableGameStats");
                const auto& achievements = member(stats,"achievements");
                const auto& gameName = member(game,"gameName");
                const auto& gameVersion = member(game,"gameVersion");
                const bool validVersion = number(gameVersion).has_value() ||
                    (gameVersion.is_string() && decimal(gameVersion.get<std::string>()));
                // 无成就游戏可能省略统计块；只有完整游戏标识才可证明空架构。
                if (status == 200 && game.is_object() && !game.contains("availableGameStats") &&
                    gameName.is_string() && !gameName.get_ref<const std::string&>().empty() && validVersion) {
                    self->result[id].achievementsTotal = 0;
                    self->result[id].achievementsUnlocked = 0;
                }
                if (status == 200 && stats.is_object()) {
                    if (!stats.contains("achievements")) self->result[id].achievementsTotal = 0;
                    else if (achievements.is_array() && std::all_of(achievements.begin(),achievements.end(),
                        [](const Json& achievement) { return achievement.is_object(); }))
                        self->result[id].achievementsTotal = static_cast<std::int64_t>(achievements.size());
                    if (self->result[id].achievementsTotal == 0)
                        self->result[id].achievementsUnlocked = 0;
                }
                if (self->result[id].achievementsTotal == 0 &&
                    self->result[id].errorCode == "steam_stats_not_supported")
                    self->result[id].errorCode.clear();
                self->next();
            });
    }
};
}
void SteamConfiguration::validate(const SteamConfig& config) {
    const bool keyValid = config.apiKey.empty() || (config.apiKey.size() == 32 &&
        std::all_of(config.apiKey.begin(),config.apiKey.end(),[](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }));
    if (!keyValid || (!config.steamId.empty() && (config.steamId.size() != 17 || !decimal(config.steamId))) ||
        config.steamPath.size() > 4096 || config.steamPath.find('\0') != std::string::npos)
        throw GameError("invalid_steam_config");
}
SteamConfiguration::SteamConfiguration(std::filesystem::path dataPath) {
    auto path = std::filesystem::absolute(std::move(dataPath)).lexically_normal().wstring();
    std::transform(path.begin(),path.end(),path.begin(),[](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    target_ = L"AnimeVault/steam/" + path;
#ifdef _WIN32
    PCREDENTIALW raw = nullptr;
    if (!CredReadW(target_.c_str(),CRED_TYPE_GENERIC,0,&raw)) {
        if (GetLastError() == ERROR_NOT_FOUND) return;
        throw GameError("steam_config_storage_failed");
    }
    const std::unique_ptr<CREDENTIALW,decltype(&CredFree)> owned(raw,&CredFree);
    try {
        const auto value = Json::parse(std::string(reinterpret_cast<const char*>(raw->CredentialBlob),raw->CredentialBlobSize));
        config_ = {value.at("steamPath").get<std::string>(),value.at("steamId").get<std::string>(),value.at("apiKey").get<std::string>()};
        validate(config_);
    } catch (...) { throw GameError("steam_config_storage_failed"); }
#endif
}
SteamConfig SteamConfiguration::current() const { std::lock_guard lock(mutex_); return config_; }
void SteamConfiguration::save(SteamConfig config) {
    std::lock_guard lock(mutex_);
    if (config.apiKey.empty()) config.apiKey = config_.apiKey;
    validate(config);
#ifdef _WIN32
    // 完整配置仅进入 Windows 凭据库，避免路径、账号或密钥写入明文配置。
    const auto blob = Json{{"steamPath",config.steamPath},{"steamId",config.steamId},{"apiKey",config.apiKey}}.dump();
    if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) throw GameError("steam_config_storage_failed");
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target_.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(blob.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&credential,0)) throw GameError("steam_config_storage_failed");
    config_ = std::move(config);
#else
    throw GameError("steam_config_storage_failed");
#endif
}
SteamClient::SteamClient(Request request) : request_(std::move(request)) {}
void SteamClient::fetch(SteamConfig config,std::vector<std::string> appIds,Completion completion) {
    SteamConfiguration::validate(config);
    auto state = std::make_shared<Fetch>();
    state->request = request_; state->completion = std::move(completion); state->config = std::move(config);
    for (const auto& id : appIds) {
        if (!decimal(id) || id.size() > 10 || id.front() == '0') throw GameError("invalid_steam_app_id");
        if (state->result.emplace(id,SteamStats{}).second) state->ids.push_back(id);
    }
    if (state->ids.empty()) { state->done(); return; }
    if (state->config.apiKey.empty() || state->config.steamId.empty()) { state->done("steam_credentials_required"); return; }
    state->enqueue([state] { state->start(); });
}
}
