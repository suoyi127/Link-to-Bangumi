#include "anime_vault/infrastructure/network/SteamMetadataClient.hpp"
#include "anime_vault/domain/Game.hpp"
#include "anime_vault/services/CoverScraper.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <regex>
namespace anime_vault {
namespace {
bool validId(const std::string& id) {
    return !id.empty() && id.size() <= 10 && id.front() != '0' &&
        std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool hasChinese(const std::string& text) {
    if (text.empty() || text.size() > 1000 || text.find('\0') != std::string::npos) return false;
    unsigned code = 0;
    for (unsigned char c : text) {
        if (c < 0x80) code = c;
        else if (c >= 0xf0) code = c & 7;
        else if (c >= 0xe0) code = c & 15;
        else if (c >= 0xc0) code = c & 31;
        else {
            code = (code << 6) | (c & 63);
            if ((code >= 0x3400 && code <= 0x9fff) || (code >= 0xf900 && code <= 0xfaff) ||
                (code >= 0x20000 && code <= 0x323af)) return true;
        }
    }
    return false;
}
struct Fetch : std::enable_shared_from_this<Fetch> {
    SteamMetadataClient::Request request;
    SteamMetadataClient::Completion completion;
    std::map<std::string, SteamMetadata> results;
    std::vector<std::string> ids;
    std::size_t position{};
    std::mutex mutex;
    std::deque<std::function<void()>> queue;
    bool pumping{}, stopped{};
    void enqueue(std::function<void()> fn) {
        std::unique_lock lock(mutex); queue.push_back(std::move(fn));
        if (pumping) return;
        pumping = true;
        while (!queue.empty()) {
            auto action = std::move(queue.front()); queue.pop_front();
            lock.unlock(); action(); lock.lock();
        }
        pumping = false;
    }
    void send(std::string host, std::string path, std::function<void(int, std::string)> consume) {
        auto self = shared_from_this();
        auto callback = [self, consume = std::move(consume)](int status, std::string body) {
            self->enqueue([self, status, body = std::move(body), consume] {
                if (self->stopped) return;
                if (status <= 0 || status == 429) {
                    self->stopped = true;
                    for (auto& [id, value] : self->results)
                        if (!value.cover) value.warnings.push_back("steam_metadata_unavailable");
                    auto complete = std::move(self->completion); complete(std::move(self->results));
                    return;
                }
                consume(status, body);
            });
        };
        try { request(std::move(host), std::move(path), callback); }
        catch (...) { callback(0, {}); }
    }
    void next() {
        if (position == ids.size()) {
            stopped = true; auto done = std::move(completion); done(std::move(results)); return;
        }
        store(ids[position++], false, {});
    }
    void store(const std::string& id, bool traditional, std::string coverUrl) {
        auto self = shared_from_this();
        send("store.steampowered.com", "/api/appdetails?appids=" + id + "&l=" + (traditional ? "tchinese" : "schinese"),
            [self, id, traditional, coverUrl = std::move(coverUrl)](int status, std::string body) mutable {
                const auto json = nlohmann::json::parse(body, nullptr, false);
                bool valid = false;
                if (status == 200 && json.is_object() && json.contains(id)) {
                    const auto& item = json[id];
                    if (item.is_object() && item.contains("success") && item["success"] == true &&
                        item.contains("data") && item["data"].is_object()) {
                        const auto& data = item["data"];
                        valid = data.contains("name") && data["name"].is_string() && !data["name"].get_ref<const std::string&>().empty();
                        if (valid && hasChinese(data["name"].get<std::string>())) self->results[id].chineseTitle = data["name"].get<std::string>();
                        if (coverUrl.empty() && data.contains("header_image") && data["header_image"].is_string()) coverUrl = data["header_image"].get<std::string>();
                    }
                }
                if (!valid && !traditional) {
                    self->results[id].warnings.push_back("steam_metadata_unavailable"); self->next(); return;
                }
                if (valid && !traditional && self->results[id].chineseTitle.empty()) {
                    self->store(id, true, std::move(coverUrl)); return;
                }
                self->cover(id, coverUrl);
            });
    }
    void cover(const std::string& id, const std::string& url) {
        const auto address = SteamMetadataClient::coverRequest(url, id);
        if (!address) { results[id].warnings.push_back("steam_cover_unavailable"); next(); return; }
        auto self = shared_from_this();
        send(address->first, address->second, [self, id](int status, std::string bytes) {
            auto image = status == 200 ? CoverScraper::detectImage(bytes) : std::nullopt;
            if (image) { image->bytes = std::move(bytes); self->results[id].cover = std::move(image); }
            else self->results[id].warnings.push_back("steam_cover_unavailable");
            self->next();
        });
    }
};
}
void SteamMetadataClient::fetch(std::vector<std::string> ids, Completion completion) {
    auto state = std::make_shared<Fetch>(); state->request = request_; state->completion = std::move(completion);
    for (const auto& id : ids) {
        if (!validId(id)) throw GameError("invalid_steam_app_id");
        if (state->results.emplace(id, SteamMetadata{}).second) state->ids.push_back(id);
    }
    state->enqueue([state] { state->next(); });
}
std::optional<std::pair<std::string, std::string>> SteamMetadataClient::coverRequest(const std::string& url, const std::string& appId) {
    if (!validId(appId) || url.size() > 2048) return {};
    // 商店返回的链接仍按域名和当前 AppID 校验，禁止任意地址、端口和路径跳转。
    static const std::regex pattern(R"(^https://((?:shared\.(?:akamai|fastly)|cdn\.(?:akamai|cloudflare))\.steamstatic\.com|steamcdn-a\.akamaihd\.net)((?:/store_item_assets)?/steam/apps/([0-9]+)/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+\.(?:jpg|jpeg|png|webp)(?:\?[A-Za-z0-9_=&.-]+)?)$)");
    std::smatch match;
    if (!std::regex_match(url, match, pattern) || match[3].str() != appId) return {};
    return std::pair{match[1].str(), match[2].str()};
}
}
