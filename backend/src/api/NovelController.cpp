#include "anime_vault/api/NovelController.hpp"
#include <drogon/drogon.h>
#include <atomic>
#include <set>

namespace anime_vault::api {
namespace {
using Request = drogon::HttpRequestPtr;
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
std::atomic_uint64_t sequence{0};
struct RemoveNovelRequest { bool confirmed{}; };
struct RemoveNovelResponse { std::int64_t removedId{}; };
void reply(Callback callback, Json::Value payload, int status = 200) {
    auto id = "novel-" + std::to_string(++sequence); payload["requestId"] = id;
    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(status)); response->addHeader("X-Request-Id", id); callback(response);
}
void failure(Callback callback, const std::string& code, int status = 400) {
    Json::Value j; j["error"]["code"] = code; j["error"]["message"] = code; reply(std::move(callback), j, status);
}
bool local(const Request& r) {
    if (r->getHeader("sec-fetch-site") == "cross-site") return false;
    const auto origin = r->getHeader("origin");
    return origin.empty() || origin == "http://127.0.0.1:5173" || origin == "http://localhost:5173" || origin == "http://127.0.0.1:8848" || origin == "http://localhost:8848";
}
const Json::Value& body(const Request& request, const std::set<std::string>& fields) {
    if (!local(request)) throw NovelError("cross_site_request_forbidden");
    if (request->body().size() > 24000) throw NovelError("request_too_large");
    const auto j = request->getJsonObject();
    if (!j || !j->isObject()) throw NovelError("invalid_request");
    for (const auto& name : j->getMemberNames()) if (!fields.contains(name)) throw NovelError("invalid_request");
    return *j;
}
std::string stringField(const Json::Value& j, const char* key) {
    if (!j[key].isString()) throw NovelError("invalid_request"); return j[key].asString();
}
std::int64_t idField(const Json::Value& j, const char* key) {
    if (!j[key].isInt64() || j[key].asInt64() <= 0) throw NovelError("invalid_request"); return j[key].asInt64();
}
Json::Value workJson(const NovelWork& w) {
    Json::Value j; j["id"] = Json::Int64(w.id); j["title"] = w.title; j["author"] = w.author; j["summary"] = w.summary;
    j["subjectId"] = w.subjectId ? Json::Value(Json::Int64(*w.subjectId)) : Json::Value();
    j["manualMetadata"] = w.manualMetadata; j["coverUrl"] = w.hasCover ? "/api/novels/" + std::to_string(w.id) + "/cover" : "";
    j["files"] = Json::Value(Json::arrayValue);
    for (const auto& f : w.files) {
        Json::Value item; item["id"] = Json::Int64(f.id); item["workId"] = Json::Int64(f.workId); item["path"] = f.path; item["label"] = f.label; item["missing"] = f.missing;
        item["subjectId"] = f.subjectId ? Json::Value(Json::Int64(*f.subjectId)) : Json::Value();
        item["coverUrl"] = f.hasCover ? "/api/novel-files/" + std::to_string(f.id) + "/cover" : ""; j["files"].append(item);
    }
    return j;
}
void handleError(Callback callback, const std::exception& error) {
    const auto* known = dynamic_cast<const NovelError*>(&error); const auto code = known ? known->code : "novel_storage_error";
    const int status = code == "novel_storage_error" ? 500 : code.find("not_found") != std::string::npos ? 404 :
        code == "cross_site_request_forbidden" ? 403 : code == "request_too_large" ? 413 : code == "novel_has_accessible_files" ? 409 : 400;
    failure(std::move(callback), code, status);
}
Json::Value scrapeJson(const NovelScrapeResult& result) {
    Json::Value j; j["bound"] = result.bound; j["coverUpdated"] = result.coverUpdated; j["errorCode"] = result.errorCode; return j;
}
}
void registerNovelEndpoints(std::shared_ptr<NovelService> novels, std::shared_ptr<NovelBangumiService> bangumi, ProcessLauncher& launcher) {
    drogon::app().registerHandler("/api/novels/{1}", [novels](const Request& r, Callback&& callback, std::int64_t id) {
        try {
            const auto& j = body(r, {"confirmed"});
            if (id <= 0 || !j["confirmed"].isBool()) throw NovelError("invalid_request");
            const RemoveNovelRequest request{j["confirmed"].asBool()};
            if (!request.confirmed) throw NovelError("confirmation_required");
            novels->removeUnavailableWork(id);
            const RemoveNovelResponse response{id}; Json::Value out; out["removedId"] = Json::Int64(response.removedId);
            reply(std::move(callback), out);
        } catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Delete});
    drogon::app().registerHandler("/api/novels", [novels](const Request&, Callback&& callback) {
        try { Json::Value j; j["items"] = Json::Value(Json::arrayValue); for (const auto& w : novels->list()) j["items"].append(workJson(w)); reply(std::move(callback), j); }
        catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Get});
    drogon::app().registerHandler("/api/novels/{1}", [novels](const Request&, Callback&& callback, std::int64_t id) {
        try { reply(std::move(callback), workJson(novels->get(id))); } catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Get});
    drogon::app().registerHandler("/api/novels/{1}", [novels](const Request& r, Callback&& callback, std::int64_t id) {
        try { const auto& j = body(r, {"title", "author", "summary"}); novels->edit(id, stringField(j, "title"), stringField(j, "author"), stringField(j, "summary")); reply(std::move(callback), workJson(novels->get(id))); }
        catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Put});
    drogon::app().registerHandler("/api/novel-files/{1}", [novels](const Request& r, Callback&& callback, std::int64_t id) {
        try { const auto& j = body(r, {"workId", "label"}); const auto work = idField(j, "workId"); novels->editFile(id, work, stringField(j, "label")); reply(std::move(callback), workJson(novels->get(work))); }
        catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Put});
    drogon::app().registerHandler("/api/novel-sources", [novels](const Request&, Callback&& callback) {
        try { Json::Value j; j["items"] = Json::Value(Json::arrayValue); for (const auto& s : novels->sources()) { Json::Value item; item["id"] = Json::Int64(s.id); item["path"] = s.path; item["directory"] = s.directory; j["items"].append(item); } reply(std::move(callback), j); }
        catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Get});
    drogon::app().registerHandler("/api/novel-imports", [novels, bangumi](const Request& r, Callback&& callback) {
        try {
            const auto& j = body(r, {"path"}); const auto path = stringField(j, "path");
            if (path.empty() || path.size() > 4096 || path.find('\0') != std::string::npos) throw NovelError("invalid_novel_path");
            const auto result = novels->importPath(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size())));
            // 批量刮削串行排队，避免一次导入大量文件时耗尽网络线程。
            const auto queued = bangumi->queue(result.workIds);
            Json::Value out; out["sourceId"] = Json::Int64(result.sourceId); out["fileCount"] = result.fileCount; out["scraping"] = queued > 0; out["queuedCount"] = Json::UInt64(queued); reply(std::move(callback), out, 202);
        } catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Post});
    drogon::app().registerHandler("/api/novel-reader", [novels](const Request&, Callback&& callback) {
        try { auto config = novels->reader(); Json::Value j; j["type"] = config.type; j["executable"] = config.executable; reply(std::move(callback), j); }
        catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Get});
    drogon::app().registerHandler("/api/novel-reader", [novels](const Request& r, Callback&& callback) {
        try { const auto& j = body(r, {"type", "executable"}); ReaderConfig config{stringField(j, "type"), stringField(j, "executable")}; novels->saveReader(config); Json::Value out; out["type"] = config.type; out["executable"] = novels->reader().executable; reply(std::move(callback), out); }
        catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Put});
    drogon::app().registerHandler("/api/novel-files/{1}/read", [novels, &launcher](const Request& r, Callback&& callback, std::int64_t id) {
        try { if (!local(r)) throw NovelError("cross_site_request_forbidden"); if (!r->body().empty()) throw NovelError("invalid_request"); novels->read(id, launcher); Json::Value j; j["started"] = true; reply(std::move(callback), j, 202); }
        catch (const std::exception& e) { handleError(std::move(callback), e); }
    }, {drogon::Post});
    drogon::app().registerHandler("/api/novel-bangumi/search", [bangumi](const Request& r, Callback&& callback) {
        bangumi->search(r->getParameter("q"), [callback = std::move(callback)](NovelSearchResult result) mutable {
            if (!result.errorCode.empty()) { failure(std::move(callback), result.errorCode, 503); return; }
            Json::Value j; j["items"] = Json::Value(Json::arrayValue);
            for (const auto& s : result.items) { Json::Value item; item["id"] = Json::Int64(s.id); item["name"] = s.name; item["nameCn"] = s.nameCn; item["platform"] = s.platform; item["series"] = s.series; item["coverUrl"] = CoverScraper::allowedImagePath(s.coverUrl) ? s.coverUrl : ""; j["items"].append(item); }
            reply(std::move(callback), j);
        });
    }, {drogon::Get});
    for (const bool file : {false, true}) {
        const std::string base = file ? "/api/novel-files/{1}" : "/api/novels/{1}";
        drogon::app().registerHandler(base + "/scrape", [bangumi, file](const Request& r, Callback&& callback, std::int64_t id) {
            try { const auto& j = body(r, {"subjectId"}); std::optional<std::int64_t> subject; if (j.isMember("subjectId")) subject = idField(j, "subjectId");
                bangumi->scrape(id, file, subject, [callback = std::move(callback)](NovelScrapeResult result) mutable { reply(std::move(callback), scrapeJson(result)); });
            } catch (const std::exception& e) { handleError(std::move(callback), e); }
        }, {drogon::Post});
        drogon::app().registerHandler(base + "/cover", [novels, file](const Request&, Callback&& callback, std::int64_t id) {
            try { auto cover = novels->cover(id, file); if (!cover) { failure(std::move(callback), "cover_unavailable", 404); return; }
                auto response = drogon::HttpResponse::newHttpResponse(); response->setContentTypeString(cover->mimeType); response->setBody(std::move(cover->bytes)); response->addHeader("Cache-Control", "no-cache"); response->addHeader("X-Content-Type-Options", "nosniff"); callback(response);
            } catch (const std::exception& e) { handleError(std::move(callback), e); }
        }, {drogon::Get});
    }
}
}
