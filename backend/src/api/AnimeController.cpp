#include "anime_vault/api/AnimeController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/api/MediaJson.hpp"

namespace anime_vault::api {
BindAnimeHttpRequest parseBindAnimeRequest(const Json::Value& body) {
    if (!body.isObject()) throw ApiError(400, "invalid_request", "JSON object required");
    for (const auto& name : body.getMemberNames())
        if (name != "subjectId" && name != "confirmed")
            throw ApiError(400, "invalid_request", "unknown binding field");
    if (!body["subjectId"].isInt64() || body["subjectId"].asInt64() <= 0)
        throw ApiError(400, "bangumi_invalid_subject_id", "subjectId must be positive");
    if (!body["confirmed"].isBool() || !body["confirmed"].asBool())
        throw ApiError(400, "confirmation_required", "explicit confirmation required");
    return {body["subjectId"].asInt64(), true};
}

void registerAnimeController(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                            std::shared_ptr<CoverScraper> covers,
                            std::shared_ptr<BangumiCalendarService> calendar) {
    drogon::app().registerHandler("/api/anime-calendar", [calendar](const Request& request, Callback&& callback) {
        const auto requestId = requestIdFor(request);
        if (!request->body().empty() || !request->getParameters().empty()) {
            deliver(std::move(callback), requestId, 400, errorJson("invalid_request", "calendar takes no parameters"));
            return;
        }
        if (!calendar) {
            deliver(std::move(callback), requestId, 503, errorJson("bangumi_calendar_unavailable", "calendar unavailable"));
            return;
        }
        calendar->get([requestId, callback = std::move(callback)](AnimeCalendarResult calendarResult) mutable {
            if (!calendarResult.errorCode.empty() && !calendarResult.stale) {
                deliver(std::move(callback), requestId, 503, errorJson(calendarResult.errorCode, "calendar unavailable"));
                return;
            }
            Json::Value payload;
            payload["days"] = Json::Value(Json::arrayValue);
            for (const auto& day : calendarResult.days) {
                Json::Value value;
                value["weekday"] = day.weekday;
                value["items"] = Json::Value(Json::arrayValue);
                for (const auto& anime : day.items) value["items"].append(animeJson(anime, false));
                payload["days"].append(std::move(value));
            }
            payload["fromCache"] = calendarResult.fromCache;
            payload["stale"] = calendarResult.stale;
            payload["updatedAt"] = Json::Int64(calendarResult.updatedAt);
            payload["errorCode"] = calendarResult.errorCode;
            deliver(std::move(callback), requestId, 200, std::move(payload));
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/anime", [&repository](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty())
                throw ApiError(400, "invalid_request", "anime list takes no body");
            for (const auto& [name, _] : request->getParameters())
                if (name != "limit" && name != "offset" && name != "sort")
                    throw ApiError(400, "invalid_page", "unknown page parameter");
            const auto limit = static_cast<int>(pageNumber(request, "limit", 50, 1, 200));
            const auto offset = pageNumber(request, "offset", 0, 0, 1'000'000);
            const auto sort = request->getParameters().contains("sort") ? request->getParameter("sort") : "import_desc";
            if (sort != "import_desc" && sort != "import_asc") throw ApiError(400, "invalid_sort", "invalid anime sort");
            const auto page = repository.listAnimePage(offset, limit, sort);
            Json::Value result;
            result["items"] = Json::Value(Json::arrayValue);
            for (const auto& anime : page.items) result["items"].append(animeJson(anime, false));
            if (page.nextOffset) result["nextOffset"] = Json::Int64(*page.nextOffset);
            else result["nextOffset"] = Json::nullValue;
            return result;
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/anime/{1}", [&repository](const Request& request, Callback&& callback, std::string id) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty())
                throw ApiError(400, "invalid_request", "anime detail takes no body");
            for (const auto& [name, _] : request->getParameters())
                if (name != "mediaLimit" && name != "mediaOffset")
                    throw ApiError(400, "invalid_page", "unknown media page parameter");
            const auto limit = static_cast<int>(pageNumber(request, "mediaLimit", 50, 1, 200));
            const auto offset = pageNumber(request, "mediaOffset", 0, 0, 1'000'000);
            const auto anime = repository.getAnime(parseId(id), offset, limit);
            if (!anime) throw ApiError(404, "anime_not_found", "anime not found");
            return animeJson(*anime, true);
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/anime/{1}", [&repository](const Request& request, Callback&& callback, std::string id) {
        respond(request, std::move(callback), [&] {
            const auto body = request->body().size() <= 8192 ? request->getJsonObject() : nullptr;
            if (!request->getParameters().empty() || !body || !body->isObject() || body->size() != 3 ||
                !(*body)["displayTitle"].isString() || !(*body)["season"].isString() || !(*body)["coverUrl"].isString())
                throw ApiError(400, "invalid_anime_edit", "displayTitle, season and coverUrl required");
            const AnimeEdit edit{(*body)["displayTitle"].asString(), (*body)["season"].asString(), (*body)["coverUrl"].asString()};
            try { return animeJson(repository.editAnime(parseId(id), edit), true); }
            catch (const AnimeBindingError& error) {
                throw ApiError(error.code() == "anime_not_found" ? 404 : 400, error.code(), "cannot edit anime");
            }
        });
    }, {drogon::Put});
    drogon::app().registerHandler("/api/bangumi/search", [bangumi](const Request& request, Callback&& callback) mutable {
        const auto requestId = requestIdFor(request);
        if (!request->body().empty() || request->getParameters().size() != 1 ||
            request->getParameters().find("q") == request->getParameters().end()) {
            deliver(std::move(callback), requestId, 400, errorJson("invalid_request", "q is required"));
            return;
        }
        const auto query = request->getParameter("q");
        if (query.empty() || query.size() > 500) {
            deliver(std::move(callback), requestId, 400,
                    errorJson("bangumi_invalid_query", "invalid query"));
            return;
        }
        bangumi->search({query}, [requestId, callback = std::move(callback)](BangumiSearchResult result) mutable {
            if (!result.errorCode.empty()) {
                auto payload = errorJson(result.errorCode, "Bangumi search unavailable");
                payload["retryCount"] = result.retryCount;
                if (result.retryAfter)
                    payload["retryAfter"] = Json::Int64(std::chrono::duration_cast<std::chrono::seconds>(
                        result.retryAfter->time_since_epoch()).count());
                deliver(std::move(callback), requestId, bangumiErrorStatus(result.errorCode), std::move(payload));
                return;
            }
            Json::Value payload;
            payload["items"] = Json::Value(Json::arrayValue);
            for (const auto& candidate : result.candidates.items)
                payload["items"].append(candidateJson(candidate));
            payload["autoBindEligible"] = result.candidates.autoBindEligible;
            payload["fromCache"] = result.fromCache;
            if (result.localMatch) {
                Json::Value local;
                local["animeId"] = Json::Int64(result.localMatch->id);
                local["displayTitle"] = result.localMatch->displayTitle;
                if (result.localMatch->bangumiSubjectId)
                    local["bangumiSubjectId"] = Json::Int64(*result.localMatch->bangumiSubjectId);
                payload["localMatch"] = std::move(local);
            } else payload["localMatch"] = Json::nullValue;
            deliver(std::move(callback), requestId, 200, std::move(payload));
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/anime/{1}/bangumi", [&repository, bangumi, covers](
        const Request& request, Callback&& callback, std::string rawId) mutable {
        const auto requestId = requestIdFor(request);
        std::int64_t animeId{};
        BindAnimeHttpRequest dto;
        try {
            animeId = parseId(rawId);
            if (request->body().size() > 4096)
                throw ApiError(400, "request_too_large", "binding request is too large");
            if (!request->getParameters().empty())
                throw ApiError(400, "invalid_request", "binding takes no query parameters");
            const auto body = request->getJsonObject();
            if (!body) throw ApiError(400, "invalid_request", "JSON object required");
            dto = parseBindAnimeRequest(*body);
            if (!repository.getAnime(animeId))
                throw ApiError(404, "anime_not_found", "anime not found");
            // 本地占用优先返回合并入口，离线及远端限流不应阻塞本地合并。
            if (const auto owner = repository.findAnimeByBangumiSubject(dto.subjectId); owner && owner->id != animeId) {
                auto payload = errorJson("bangumi_subject_in_use", "anime binding cannot be completed");
                payload["targetAnimeId"] = Json::Int64(owner->id);
                payload["targetTitle"] = owner->displayTitle;
                deliver(std::move(callback), requestId, 409, std::move(payload));
                return;
            }
        } catch (const ApiError& error) {
            deliver(std::move(callback), requestId, error.status, errorJson(error.code, error.what()));
            return;
        } catch (...) {
            deliver(std::move(callback), requestId, 500, errorJson("internal_error", "internal error"));
            return;
        }
        bangumi->subject(dto.subjectId, [&repository, covers, animeId, requestId,
                                         callback = std::move(callback)](BangumiSubjectResult result) mutable {
            if (!result.errorCode.empty()) {
                deliver(std::move(callback), requestId, bangumiErrorStatus(result.errorCode),
                        errorJson(result.errorCode, "Bangumi subject unavailable"));
                return;
            }
            try {
                const auto bound = repository.bindAnime(animeId, *result.subject);
                if (covers && !result.subject->coverUrl.empty() && bound.coverUrl == result.subject->coverUrl)
                    covers->cacheSubject(animeId, *result.subject, {}, bound.coverUrl);
                auto payload = animeJson(bound, true);
                deliver(std::move(callback), requestId, 200, std::move(payload));
            } catch (const AnimeBindingError& error) {
                const int status = error.code() == "anime_not_found" ? 404 :
                    error.code() == "invalid_id" || error.code() == "invalid_subject" ? 400 : 409;
                auto payload = errorJson(error.code(), "anime binding cannot be completed");
                if (error.targetAnimeId > 0) {
                    payload["targetAnimeId"] = Json::Int64(error.targetAnimeId);
                    payload["targetTitle"] = error.targetTitle;
                }
                deliver(std::move(callback), requestId, status, std::move(payload));
            } catch (...) {
                deliver(std::move(callback), requestId, 500, errorJson("internal_error", "internal error"));
            }
        });
    }, {drogon::Put});
    for (const bool preview : {true, false}) {
        drogon::app().registerHandler(preview ? "/api/anime/{1}/merge/preview" : "/api/anime/{1}/merge",
            [&repository, preview](const Request& request, Callback&& callback, std::string rawId) {
            respond(request, std::move(callback), [&]() -> Json::Value {
                const auto id = parseId(rawId);
                if (request->body().size() > 4096) throw ApiError(400, "request_too_large", "merge request too large");
                const auto body = request->getJsonObject();
                if (!body || !body->isObject() || !request->getParameters().empty())
                    throw ApiError(400, "invalid_request", "JSON object required");
                try {
                    if (preview) {
                        if (body->size() != 1 || !(*body)["subjectId"].isInt64() || (*body)["subjectId"].asInt64() <= 0)
                            throw ApiError(400, "invalid_request", "subjectId required");
                        const PreviewAnimeMergeHttpRequest dto{(*body)["subjectId"].asInt64()};
                        const auto item = repository.previewAnimeMerge(id, dto.subjectId);
                        Json::Value result;
                        result["previewId"] = item.previewId;
                        result["sourceId"] = Json::Int64(item.sourceId); result["targetId"] = Json::Int64(item.targetId);
                        result["subjectId"] = Json::Int64(item.subjectId);
                        result["sourceTitle"] = item.sourceTitle; result["targetTitle"] = item.targetTitle;
                        result["mediaCount"] = Json::Int64(item.mediaCount); result["aliasCount"] = Json::Int64(item.aliasCount);
                        result["duplicateEpisodeCount"] = Json::Int64(item.duplicateEpisodeCount);
                        result["expiresAt"] = Json::Int64(item.expiresAt);
                        return result;
                    }
                    if (body->size() != 2 || !(*body)["previewId"].isString() ||
                        !(*body)["confirmed"].isBool() || !(*body)["confirmed"].asBool())
                        throw ApiError(400, "invalid_request", "confirmed preview required");
                    const ConfirmAnimeMergeHttpRequest dto{(*body)["previewId"].asString(), true};
                    if (dto.previewId.size() != 32 || dto.previewId.find_first_not_of("0123456789abcdef") != std::string::npos)
                        throw ApiError(400, "invalid_request", "invalid previewId");
                    return animeJson(repository.mergeAnime(id, dto.previewId), true);
                } catch (const AnimeBindingError& error) {
                    const auto& code = error.code();
                    throw ApiError(code == "anime_not_found" || code == "merge_target_not_found" ? 404 :
                        code == "merge_too_large" ? 413 : 409, code, "anime merge cannot be completed");
                }
            });
        }, {drogon::Post});
    }
    drogon::app().registerHandler("/api/anime/{1}/cover/refresh", [covers](
        const Request& request, Callback&& callback, std::string rawId) mutable {
        const auto requestId = requestIdFor(request);
        if (!covers) {
            deliver(std::move(callback), requestId, 503,
                    errorJson("cover_unavailable", "cover scraper unavailable"));
            return;
        }
        std::int64_t animeId{};
        try {
            animeId = parseId(rawId);
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "cover refresh takes no input");
        } catch (const ApiError& error) {
            deliver(std::move(callback), requestId, error.status, errorJson(error.code, error.what()));
            return;
        }
        covers->refresh(animeId, [requestId, callback = std::move(callback)](
            CoverScrapeResult result) mutable {
            if (!result.errorCode.empty()) {
                const int status = result.errorCode == "anime_not_found" ? 404 :
                    result.errorCode == "cover_subject_unbound" ? 409 : 503;
                deliver(std::move(callback), requestId, status,
                        errorJson(result.errorCode, "cover refresh unavailable"));
                return;
            }
            Json::Value payload;
            payload["coverUrl"] = result.coverUrl;
            deliver(std::move(callback), requestId, 200, std::move(payload));
        });
    }, {drogon::Post});
    drogon::app().registerHandler("/api/covers/{1}/{2}/{3}", [covers](
        const Request& request, Callback&& callback, std::string rawAnimeId,
        std::string rawSubjectId, std::string filename) mutable {
        std::optional<CachedCover> image;
        try {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "cover read takes no input");
            if (covers) image = covers->readCached(parseId(rawAnimeId), parseId(rawSubjectId), filename);
        } catch (...) { image.reset(); }
        if (!image) {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setStatusCode(drogon::k404NotFound);
            callback(response);
            return;
        }
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k200OK);
        response->setContentTypeString(image->mimeType);
        response->addHeader("Cache-Control", "private, max-age=86400");
        response->addHeader("X-Content-Type-Options", "nosniff");
        response->setBody(std::move(image->bytes));
        callback(response);
    }, {drogon::Get});
}
}
