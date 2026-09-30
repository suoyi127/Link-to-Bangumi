#include "anime_vault/services/NovelBangumiService.hpp"
#include "anime_vault/services/TitleNormalization.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <algorithm>

namespace anime_vault {
namespace {
std::string stringValue(const nlohmann::json& j, const char* key) {
    auto i = j.find(key); return i != j.end() && i->is_string() ? i->get<std::string>() : "";
}
bool novelPlatform(const std::string& value) { return value.find("小说") != std::string::npos || value.find("小説") != std::string::npos; }
bool mangaPlatform(const std::string& value) { return value.find("漫画") != std::string::npos || value.find("マンガ") != std::string::npos; }
std::string errorFor(const std::optional<BangumiTransport::Response>& response, const std::string& error) {
    if (!error.empty()) return error;
    if (!response) return "network_error";
    if (response->status != 200) return response->status == 429 ? "bangumi_rate_limited" : "bangumi_http_error";
    return {};
}
}
NovelSubject NovelBangumiService::parseSubject(const std::string& body) {
    try {
        const auto j = nlohmann::json::parse(body);
        if (!j.is_object() || !j.contains("id") || !j["id"].is_number_integer() || j["id"].get<std::int64_t>() <= 0 || j.value("type", 0) != 1) throw NovelError("bangumi_not_book");
        NovelSubject s; s.id = j["id"].get<std::int64_t>(); s.name = stringValue(j, "name"); s.nameCn = stringValue(j, "name_cn"); s.summary = stringValue(j, "summary"); s.platform = stringValue(j, "platform");
        s.series = j.value("series", false);
        if (j.contains("images") && j["images"].is_object()) { s.coverUrl = stringValue(j["images"], "large"); if (s.coverUrl.empty()) s.coverUrl = stringValue(j["images"], "common"); }
        if (j.contains("infobox") && j["infobox"].is_array()) for (const auto& info : j["infobox"]) {
            if (!info.is_object() || !info.contains("value")) continue;
            const auto key = stringValue(info, "key"); const auto& value = info["value"];
            auto values = std::vector<std::string>{};
            if (value.is_string()) values.push_back(value.get<std::string>());
            else if (value.is_array()) for (const auto& v : value) { if (v.is_object()) { auto text = stringValue(v, "v"); if (!text.empty()) values.push_back(std::move(text)); } }
            if (key == "别名") s.aliases.insert(s.aliases.end(), values.begin(), values.end());
            if (key == "作者" || key == "原作") for (const auto& v : values) { if (!s.author.empty()) s.author += " / "; s.author += v; }
        }
        if (s.name.empty() && s.nameCn.empty()) throw NovelError("bangumi_invalid_response");
        return s;
    } catch (const NovelError&) { throw; }
    catch (...) { throw NovelError("bangumi_invalid_response"); }
}
std::optional<std::int64_t> NovelBangumiService::uniqueMatch(const std::string& title, const std::vector<NovelSubject>& subjects) {
    const auto normalized = normalizeTitle(title); if (normalized.empty()) return {};
    std::set<std::int64_t> matches;
    for (const auto& s : subjects) {
        // 书籍同时包含漫画；未知分类只供人工选择，不依据标签猜测小说。
        if (!novelPlatform(s.platform) || mangaPlatform(s.platform)) continue;
        bool match = normalizeTitle(s.name) == normalized || normalizeTitle(s.nameCn) == normalized;
        for (const auto& alias : s.aliases) match = match || normalizeTitle(alias) == normalized;
        if (match) matches.insert(s.id);
    }
    return matches.size() == 1 ? std::optional<std::int64_t>(*matches.begin()) : std::nullopt;
}
void NovelBangumiService::search(std::string query, SearchCompletion completion) {
    if (query.empty() || query.size() > 200) { completion({{}, "invalid_bangumi_query"}); return; }
    auto self = shared_from_this();
    transport_.searchBooks(query, [self, query, completion = std::move(completion)](auto response, auto error) mutable {
        if (auto code = errorFor(response, error); !code.empty()) { completion({{}, code}); return; }
        try {
            const auto j = nlohmann::json::parse(response->body);
            if (!j.contains("data") || !j["data"].is_array()) throw NovelError("bangumi_invalid_response");
            NovelSearchResult result;
            for (const auto& item : j["data"]) {
                try { auto s = parseSubject(item.dump()); if (!mangaPlatform(s.platform)) result.items.push_back(std::move(s)); }
                catch (const NovelError&) { continue; }
                if (result.items.size() >= 20) break;
            }
            completion(std::move(result));
        } catch (...) { completion({{}, "bangumi_invalid_response"}); }
    });
}
std::size_t NovelBangumiService::queue(const std::vector<std::int64_t>& workIds) {
    std::size_t added = 0;
    const std::set<std::int64_t> requested(workIds.begin(), workIds.end());
    for (const auto& work : novels_.list()) {
        const auto id = work.id;
        if (!requested.contains(id)) continue;
        if (work.subjectId && work.hasCover) continue;
        std::lock_guard lock(mutex_);
        if (pending_.size() < 20000 && std::find(pending_.begin(), pending_.end(), id) == pending_.end()) { pending_.push_back(id); ++added; }
    }
    pump(); return added;
}
void NovelBangumiService::pump() {
    { std::lock_guard lock(mutex_); if (pumping_) return; pumping_ = true; }
    // 一次只推进一本书；同步失败由外层循环继续，避免未配置时递归耗尽栈。
    auto self = shared_from_this();
    for (;;) {
        std::int64_t id;
        { std::lock_guard lock(mutex_);
          if (batchActive_ || pending_.empty()) { pumping_ = false; return; }
          id = pending_.front(); pending_.pop_front(); batchActive_ = true;
        }
        try {
            scrape(id, false, {}, [self](NovelScrapeResult) {
                { std::lock_guard lock(self->mutex_); self->batchActive_ = false; }
                self->pump();
            });
        } catch (...) { std::lock_guard lock(mutex_); batchActive_ = false; }
    }
}
void NovelBangumiService::scrape(std::int64_t id, bool file, std::optional<std::int64_t> subjectId, Completion completion) {
    std::uint64_t generation;
    { std::lock_guard lock(mutex_); generation = ++generations_[{id, file}]; }
    if (subjectId) { bindSubject(id, file, *subjectId, true, generation, std::move(completion)); return; }
    std::string title; std::optional<std::int64_t> bound;
    bool requireSeries = false;
    try {
        if (!file) { const auto work = novels_.get(id); title = work.title; bound = work.subjectId; requireSeries = work.files.size() > 1; }
        else for (const auto& work : novels_.list()) for (const auto& f : work.files) if (f.id == id) { title = f.label; bound = f.subjectId; }
        if (title.empty()) { completion({"novel_not_found"}); return; }
    } catch (const NovelError& e) { completion({e.code}); return; }
    if (bound) { bindSubject(id, file, *bound, true, generation, std::move(completion)); return; }
    auto self = shared_from_this();
    search(title, [self, id, file, title, requireSeries, generation, completion = std::move(completion)](NovelSearchResult result) mutable {
        if (!result.errorCode.empty()) { completion({result.errorCode}); return; }
        if (requireSeries) std::erase_if(result.items, [](const auto& s) { return !s.series; });
        if (const auto match = uniqueMatch(title, result.items)) self->bindSubject(id, file, *match, false, generation, std::move(completion));
        else completion({"novel_match_requires_confirmation"});
    });
}
void NovelBangumiService::bindSubject(std::int64_t id, bool file, std::int64_t subjectId, bool manual, std::uint64_t generation, Completion completion) {
    auto self = shared_from_this();
    transport_.subject(subjectId, [self, id, file, subjectId, manual, generation, completion = std::move(completion)](auto response, auto error) mutable {
        if (auto code = errorFor(response, error); !code.empty()) { completion({code}); return; }
        try {
            const auto subject = parseSubject(response->body);
            if (subject.id != subjectId || mangaPlatform(subject.platform) || (!manual && !novelPlatform(subject.platform))) throw NovelError("bangumi_not_novel");
            {
                // 较早的网络响应不能覆盖用户随后发起的绑定或刷新。
                std::lock_guard lock(self->mutex_);
                if (self->generations_[{id, file}] != generation) throw NovelError("novel_scrape_superseded");
                self->novels_.bind(id, file, subject.id, subject.nameCn.empty() ? subject.name : subject.nameCn, subject.author, subject.summary, manual);
            }
            if (!CoverScraper::allowedImagePath(subject.coverUrl)) { completion({"cover_unavailable", true}); return; }
            self->covers_.fetch(subject.coverUrl, [self, id, file, subjectId, generation, completion = std::move(completion)](auto bytes, auto coverError) mutable {
                if (!bytes) { completion({coverError.empty() ? "cover_unavailable" : coverError, true}); return; }
                auto format = CoverScraper::detectImage(*bytes);
                if (!format) { completion({"cover_invalid_image", true}); return; }
                try {
                    bool updated;
                    { std::lock_guard lock(self->mutex_); updated = self->generations_[{id, file}] == generation && self->novels_.setCover(id, file, subjectId, *bytes, format->mimeType); }
                    completion({updated ? "" : "cover_binding_changed", true, updated});
                } catch (...) { completion({"novel_storage_error", true}); }
            });
        } catch (const NovelError& e) { completion({e.code}); }
        catch (...) { completion({"bangumi_invalid_response"}); }
    });
}
}
