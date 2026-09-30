#include "anime_vault/services/GameBangumiService.hpp"
#include "anime_vault/services/TitleNormalization.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <regex>
namespace anime_vault {
namespace {
std::string value(const nlohmann::json& j, const char* key) { const auto i = j.find(key); return i != j.end() && i->is_string() ? i->get<std::string>() : ""; }
std::string errorFor(const std::optional<BangumiTransport::Response>& response, const std::string& error) {
    if (!error.empty()) return error;
    if (!response) return "network_error";
    return response->status == 200 ? "" : response->status == 429 ? "bangumi_rate_limited" : "bangumi_http_error";
}
}
GameSubject GameBangumiService::parseSubject(const std::string& body) {
    try {
        const auto j = nlohmann::json::parse(body);
        if (!j.is_object() || j.value("type", 0) != 4) throw GameError("bangumi_not_game");
        GameSubject subject; subject.id = j.at("id").get<std::int64_t>();
        if (subject.id <= 0) throw GameError("bangumi_invalid_response");
        subject.name = value(j, "name"); subject.nameCn = value(j, "name_cn"); subject.summary = value(j, "summary"); subject.platform = value(j, "platform");
        if (subject.name.empty() && subject.nameCn.empty()) throw GameError("bangumi_invalid_response");
        if (j.contains("images") && j["images"].is_object()) { subject.coverUrl = value(j["images"], "large"); if (subject.coverUrl.empty()) subject.coverUrl = value(j["images"], "common"); }
        if (j.contains("infobox") && j["infobox"].is_array()) for (const auto& info : j["infobox"]) {
            if (!info.is_object() || !info.contains("value")) continue;
            std::vector<std::string> values; const auto& field = info["value"];
            if (field.is_string()) values.push_back(field.get<std::string>());
            else if (field.is_array()) for (const auto& alias : field) if (alias.is_object()) { auto name = value(alias, "v"); if (!name.empty()) values.push_back(std::move(name)); }
            const auto key = value(info, "key");
            if (key == "别名") subject.aliases.insert(subject.aliases.end(), values.begin(), values.end());
            if (key == "开发" || key == "开发商" || key == "开发公司") for (const auto& developer : values) { if (!subject.developer.empty()) subject.developer += " / "; subject.developer += developer; }
        }
        return subject;
    } catch (const GameError&) { throw; } catch (...) { throw GameError("bangumi_invalid_response"); }
}
std::optional<std::int64_t> GameBangumiService::uniqueMatch(const std::string& title, const std::vector<GameSubject>& subjects) {
    const auto normalized = normalizeTitle(title); if (normalized.empty()) return {};
    std::set<std::int64_t> matches;
    for (const auto& subject : subjects) {
        bool match = normalizeTitle(subject.name) == normalized || normalizeTitle(subject.nameCn) == normalized;
        for (const auto& alias : subject.aliases) match = match || normalizeTitle(alias) == normalized;
        if (match) matches.insert(subject.id);
    }
    return matches.size() == 1 ? std::optional<std::int64_t>(*matches.begin()) : std::nullopt;
}
void GameBangumiService::search(std::string query, std::function<void(GameSearchResult)> completion) {
    if (query.empty() || query.size() > 200) { completion({{}, "invalid_bangumi_query"}); return; }
    auto self = shared_from_this();
    transport_.searchGames(std::move(query), [self, completion = std::move(completion)](auto response, auto error) mutable {
        if (auto code = errorFor(response, error); !code.empty()) { completion({{}, code}); return; }
        try {
            const auto j = nlohmann::json::parse(response->body);
            if (!j.contains("data") || !j["data"].is_array()) throw GameError("bangumi_invalid_response");
            GameSearchResult result;
            for (const auto& item : j["data"]) {
                try { result.items.push_back(parseSubject(item.dump())); } catch (const GameError&) { continue; }
                if (result.items.size() == 20) break;
            }
            completion(std::move(result));
        } catch (...) { completion({{}, "bangumi_invalid_response"}); }
    });
}
void GameBangumiService::scrape(std::int64_t id, std::optional<std::int64_t> subjectId, Completion completion) {
    GameResource game;
    try { game = games_.get(id); } catch (const GameError& error) { completion({error.code}); return; }
    if (!subjectId && game.metadataSource == "vndb" && !game.vndbId.empty()) { scrapeVndb(id, game.vndbId, std::move(completion)); return; }
    std::uint64_t generation;
    { std::lock_guard lock(mutex_); generation = ++generations_[id]; }
    if (subjectId || game.subjectId) { bindSubject(id, subjectId.value_or(game.subjectId.value_or(0)), generation, std::move(completion)); return; }
    auto self = shared_from_this();
    search(game.title, [self, id, title = game.title, generation, completion = std::move(completion)](GameSearchResult result) mutable {
        const auto match = result.errorCode.empty() ? uniqueMatch(title, result.items) : std::nullopt;
        if (!match) {
            if (self->vndb_) self->scrapeVndbFor(id, {}, generation, std::move(completion));
            else completion({result.errorCode.empty() ? "game_match_requires_confirmation" : result.errorCode});
            return;
        }
        self->bindSubject(id, *match, generation, [self, id, generation, completion = std::move(completion)](GameScrapeResult result) mutable {
            if (!result.bound && result.errorCode != "game_scrape_superseded" && self->vndb_) self->scrapeVndbFor(id, {}, generation, std::move(completion));
            else completion(std::move(result));
        });
    });
}
void GameBangumiService::bindSubject(std::int64_t id, std::int64_t subjectId, std::uint64_t generation, Completion completion) {
    auto self = shared_from_this();
    transport_.subject(subjectId, [self, id, subjectId, generation, completion = std::move(completion)](auto response, auto error) mutable {
        if (auto code = errorFor(response, error); !code.empty()) { completion({code}); return; }
        try {
            const auto subject = parseSubject(response->body);
            if (subject.id != subjectId) throw GameError("bangumi_invalid_response");
            {
                // 新绑定优先；早到的搜索或封面响应不能覆盖后续人工选择。
                std::lock_guard lock(self->mutex_);
                if (self->generations_[id] != generation) throw GameError("game_scrape_superseded");
                self->games_.bind(id, subjectId, subject.nameCn.empty() ? subject.name : subject.nameCn, subject.developer, subject.summary, subject.platform);
            }
            if (!CoverScraper::allowedImagePath(subject.coverUrl)) { completion({"cover_unavailable", true}); return; }
            self->covers_.fetch(subject.coverUrl, [self, id, subjectId, generation, completion = std::move(completion)](auto bytes, auto error) mutable {
                if (!bytes) { completion({error.empty() ? "cover_unavailable" : error, true}); return; }
                const auto format = CoverScraper::detectImage(*bytes);
                if (!format) { completion({"cover_invalid_image", true}); return; }
                try {
                    bool updated;
                    { std::lock_guard lock(self->mutex_); updated = self->generations_[id] == generation && self->games_.setCover(id, subjectId, *bytes, format->mimeType); }
                    completion({updated ? "" : "cover_binding_changed", true, updated});
                } catch (...) { completion({"game_storage_error", true}); }
            });
        } catch (const GameError& error) { completion({error.code}); }
        catch (...) { completion({"bangumi_invalid_response"}); }
    });
}
VndbSubject GameBangumiService::parseVndbSubject(const std::string& body) {
    try {
        const auto j = nlohmann::json::parse(body); VndbSubject subject;
        subject.id = value(j, "id"); subject.name = value(j, "title");
        if (!std::regex_match(subject.id, std::regex("^v[1-9][0-9]{0,9}$")) || subject.name.empty()) throw GameError("vndb_invalid_response");
        if (j.contains("aliases") && j["aliases"].is_array()) for (const auto& alias : j["aliases"]) if (alias.is_string()) subject.aliases.push_back(alias.get<std::string>());
        const auto alternate = value(j, "alttitle"); if (!alternate.empty()) subject.aliases.push_back(alternate);
        if (j.contains("titles") && j["titles"].is_array()) for (const auto& title : j["titles"]) {
            const auto name = value(title, "title"), latin = value(title, "latin"), language = value(title, "lang");
            if (!name.empty()) subject.aliases.push_back(name);
            if (!latin.empty()) subject.aliases.push_back(latin);
            if (!name.empty() && (language == "zh-Hans" || (subject.nameCn.empty() && (language == "zh-Hant" || language == "zh")))) subject.nameCn = name;
        }
        // VNDB 简介为 BBCode；以纯文本保存，界面不会解析为 HTML 或外部嵌入。
        subject.summary = std::regex_replace(value(j, "description"), std::regex("\\[[^\\]\\r\\n]*\\]"), "");
        if (j.contains("developers") && j["developers"].is_array()) for (const auto& developer : j["developers"]) {
            auto name = value(developer, "name"); if (!name.empty()) { if (!subject.developer.empty()) subject.developer += " / "; subject.developer += name; }
        }
        if (j.contains("platforms") && j["platforms"].is_array()) for (const auto& platform : j["platforms"]) if (platform.is_string()) {
            if (!subject.platform.empty()) subject.platform += " / "; subject.platform += platform.get<std::string>();
        }
        if (j.contains("image") && j["image"].is_object()) subject.coverUrl = value(j["image"], "url");
        return subject;
    } catch (const GameError&) { throw; } catch (...) { throw GameError("vndb_invalid_response"); }
}
std::optional<std::string> GameBangumiService::uniqueVndbMatch(const std::string& title, const std::vector<VndbSubject>& subjects) {
    const auto normalized = normalizeTitle(title); if (normalized.empty()) return {};
    std::set<std::string> matches;
    for (const auto& subject : subjects) {
        bool match = normalizeTitle(subject.name) == normalized || normalizeTitle(subject.nameCn) == normalized;
        for (const auto& alias : subject.aliases) match = match || normalizeTitle(alias) == normalized;
        if (match) matches.insert(subject.id);
    }
    return matches.size() == 1 ? std::optional<std::string>(*matches.begin()) : std::nullopt;
}
void GameBangumiService::searchVndb(std::string query, std::function<void(VndbSearchResult)> completion) {
    if (query.empty() || query.size() > 200 || query.find('\0') != std::string::npos) { completion({{}, "invalid_vndb_query"}); return; }
    if (!vndb_) { completion({{}, "vndb_unavailable"}); return; }
    auto self = shared_from_this();
    vndb_->search(std::move(query), [self, completion = std::move(completion)](auto response, auto error) mutable {
        if (!error.empty() || !response || response->status != 200) { completion({{}, !error.empty() ? error : response && response->status == 429 ? "vndb_rate_limited" : "vndb_http_error"}); return; }
        try {
            const auto j = nlohmann::json::parse(response->body);
            if (!j.contains("results") || !j["results"].is_array()) throw GameError("vndb_invalid_response");
            VndbSearchResult result; result.more = j.value("more", false);
            for (const auto& item : j["results"]) { result.items.push_back(parseVndbSubject(item.dump())); if (result.items.size() == 20) break; }
            completion(std::move(result));
        } catch (...) { completion({{}, "vndb_invalid_response"}); }
    });
}
void GameBangumiService::scrapeVndb(std::int64_t id, std::optional<std::string> subjectId, Completion completion) {
    std::uint64_t generation;
    { std::lock_guard lock(mutex_); generation = ++generations_[id]; }
    scrapeVndbFor(id, std::move(subjectId), generation, std::move(completion));
}
void GameBangumiService::scrapeVndbFor(std::int64_t id, std::optional<std::string> subjectId, std::uint64_t generation, Completion completion) {
    if (!vndb_) { completion({"vndb_unavailable", false, false, "vndb"}); return; }
    GameResource game;
    try { game = games_.get(id); } catch (const GameError& error) { completion({error.code, false, false, "vndb"}); return; }
    bool superseded;
    { std::lock_guard lock(mutex_); superseded = generations_[id] != generation; }
    if (superseded) { completion({"game_scrape_superseded", false, false, "vndb"}); return; }
    if (!subjectId && !game.vndbId.empty()) subjectId = game.vndbId;
    if (subjectId) { bindVndbSubject(id, *subjectId, generation, std::move(completion)); return; }
    auto self = shared_from_this();
    searchVndb(game.title, [self, id, title = game.title, generation, completion = std::move(completion)](VndbSearchResult result) mutable {
        if (!result.errorCode.empty()) { completion({result.errorCode, false, false, "vndb"}); return; }
        const auto match = result.more ? std::nullopt : uniqueVndbMatch(title, result.items);
        if (match) self->bindVndbSubject(id, *match, generation, std::move(completion));
        else completion({"vndb_match_requires_confirmation", false, false, "vndb"});
    });
}
void GameBangumiService::bindVndbSubject(std::int64_t id, std::string subjectId, std::uint64_t generation, Completion completion) {
    if (!std::regex_match(subjectId, std::regex("^v[1-9][0-9]{0,9}$"))) { completion({"invalid_vndb_id", false, false, "vndb"}); return; }
    auto self = shared_from_this();
    vndb_->subject(subjectId, [self, id, subjectId, generation, completion = std::move(completion)](auto response, auto error) mutable {
        if (!error.empty() || !response || response->status != 200) { completion({!error.empty() ? error : response && response->status == 429 ? "vndb_rate_limited" : "vndb_http_error", false, false, "vndb"}); return; }
        try {
            const auto j = nlohmann::json::parse(response->body);
            if (!j.contains("results") || !j["results"].is_array() || j["results"].size() != 1) throw GameError("vndb_subject_not_found");
            const auto subject = parseVndbSubject(j["results"][0].dump());
            if (subject.id != subjectId) throw GameError("vndb_invalid_response");
            {
                std::lock_guard lock(self->mutex_);
                if (self->generations_[id] != generation) throw GameError("game_scrape_superseded");
                self->games_.bindVndb(id, subjectId, subject.nameCn.empty() ? subject.name : subject.nameCn, subject.developer, subject.summary, subject.platform);
            }
            if (!VndbTransport::allowedCoverPath(subject.coverUrl)) { completion({"vndb_cover_unavailable", true, false, "vndb"}); return; }
            self->vndb_->fetch(subject.coverUrl, [self, id, subjectId, generation, completion = std::move(completion)](auto bytes, auto error) mutable {
                if (!bytes) { completion({error.empty() ? "vndb_cover_unavailable" : error, true, false, "vndb"}); return; }
                const auto format = CoverScraper::detectImage(*bytes);
                if (!format) { completion({"cover_invalid_image", true, false, "vndb"}); return; }
                try {
                    bool updated;
                    { std::lock_guard lock(self->mutex_); updated = self->generations_[id] == generation && self->games_.setVndbCover(id, subjectId, *bytes, format->mimeType); }
                    completion({updated ? "" : "cover_binding_changed", true, updated, "vndb"});
                } catch (...) { completion({"game_storage_error", true, false, "vndb"}); }
            });
        } catch (const GameError& error) { completion({error.code, false, false, "vndb"}); }
        catch (...) { completion({"vndb_invalid_response", false, false, "vndb"}); }
    });
}
}
