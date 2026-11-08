#include "anime_vault/api/QbRssController.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "anime_vault/services/MikanChinese.hpp"
#include <sqlite3.h>
#include <atomic>
#include <drogon/drogon.h>
#include <random>
namespace {
using Json = nlohmann::json;
using namespace anime_vault;
const std::string feed = "https://mikanani.me/RSS/MyBangumi?token=test";
Json article(std::string title, int id) {
    return {{"title", title}, {"torrentURL", "https://mikanani.me/Download/" + std::to_string(id) + ".torrent"}};
}
std::string items(Json entries) { return Json{{"folder", {{"feed", {{"url", feed}, {"articles", entries}}}}}}.dump(); }
TEST_CASE("Mikan matching isolates seasons types and titles within an aggregate feed") {
    const auto data = items({article("[A] Show S01E03 [720p]", 1), article("[B] Show S01E03v2 [1080p]", 2),
                             article("[A] Show S02E03 [720p]", 3), article("[A] Other S01E03 [720p]", 4),
                             article("[A] Show - SP03 [720p]", 5), article("[A] Show - 01-12 [1080p]", 6),
                             article("[A] Show - 03 [720p] REPACK", 7), article("[A] Show - NCOP03 [720p]", 8)});
    const auto candidates = selectMikanDownloads({feed, "B", "1080p"}, data, "[]");
    REQUIRE(candidates.size() == 4);
    REQUIRE(std::any_of(candidates.begin(), candidates.end(),
                        [](const auto &c) { return c.score == 2 && c.revision == 2; }));
}
TEST_CASE("Mikan confirmed bilingual aliases group the matching episode only") {
    auto entries = items({article("[A] 示例 / Sample - 03 [1080p]", 1), article("[B] Sample - 03 [720p]", 2),
                          article("[C] Sample Story - 03 [720p]", 3)});
    auto selected =
        selectMikanDownloads({feed, "", ""}, entries, R"([{"name":"[Old] Sample - 03 [480p].mkv","hash":"abc"}])");
    REQUIRE(selected.size() == 2);
    REQUIRE(std::count_if(selected.begin(), selected.end(), [](const auto &c) { return c.downloaded; }) == 1);
}
TEST_CASE("Mikan history follows a new bilingual alias across feeds and restart") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("av-mikan-store-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directory(dir);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir};
    MikanSubscription a{feed, "", ""}, b{feed + "2", "", ""};
    MikanCandidate original{R"(["sample",1,0,"3"])", "old", "old title", "", "", 0, 1};
    {
        SqliteDatabase db(dir / "download.sqlite");
        db.migrate();
        MikanDownloadStore store(db);
        store.record("local", a, original);
        original.url = "pending";
        original.identity = R"(["second",1,0,"4"])";
        REQUIRE(store.reserve("local", a, original));
    }
    SqliteDatabase db(dir / "download.sqlite");
    db.migrate();
    MikanDownloadStore store(db);
    REQUIRE(store.pending("local", a).size() == 1);
    store.saveAliases("local", b.url, {{"sample", {"示例"}}});
    MikanCandidate translated{R"(["示例",1,0,"3"])", "new", "new title", "", "", 0, 1};
    REQUIRE_FALSE(store.wanted("local", b, translated));
    translated.identity = R"(["示例",2,0,"3"])";
    REQUIRE(store.wanted("local", b, translated));
}
TEST_CASE("Mikan folds traditional titles and ranks explicit subtitle tags per episode") {
    const auto data = items(
        {article("[Group] 進擊的巨人 S02E03 [CHT]", 1), article("[Group] 进击的巨人 S02E03 [CHS]", 2),
         article("[Group] 進擊的巨人 S02E04 [簡繁日字幕]", 3), article("[简体字幕组] 进击的巨人 S02E05 [1080p]", 4)});
    for (const auto &script : {"chs", "cht"}) {
        const auto releases = selectMikanDownloads({feed, "", "", script}, data, "[]");
        REQUIRE(releases.size() == 3);
        REQUIRE(releases[0].url.ends_with(std::string(script) == "chs" ? "/2.torrent" : "/1.torrent"));
        REQUIRE(releases[0].score == 1);
        REQUIRE(releases[1].score == 1);
        REQUIRE(releases[2].subtitleScripts == 0);
    }
}
TEST_CASE("Explicit tags before the title are preferences, not separate shows") {
    auto data = items({article("[G] [简体] Show - 03 [1080p]", 1), article("[G] [繁體] Show - 03 [1080p]", 2)});
    const auto rows = selectMikanDownloads({feed, "", "", "cht"}, data, "[]");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows.front().url.ends_with("/2.torrent"));
}
TEST_CASE("Subtitle script labels are independent from title script and group names") {
    REQUIRE(mikanSubtitleScripts("[繁體字幕組] 進擊的巨人 - 03 [1080p]") == 0);
    REQUIRE(mikanSubtitleScripts("[Group] [CHT] Show - 03") == 2);
    REQUIRE(mikanSubtitleScripts("[Group] Show - 03 [zh-Hans]") == 1);
    REQUIRE(mikanSubtitleScripts("[Group] Show - 03 [CHS_CHT]") == 3);
    REQUIRE(mikanSubtitleScripts("[Group] Show - 03 [雙語]") == 0);
    REQUIRE(mikanSubtitleScripts("[Group] Show - 03 [2GB]") == 0);
    REQUIRE(mikanSubtitleScripts("[Group] Show - 03 [1.4 GB]") == 0);
    REQUIRE(mikanSubtitleScripts("[Group] Show - 03 [简&繁]") == 3);
    REQUIRE(mikanSubtitleScripts("[Group] Show - 03 [繁內嵌]") == 2);
}
TEST_CASE("Version 14 traditional download history still deduplicates simplified titles") {
    SqliteDatabase db(":memory:");
    db.migrate();
    REQUIRE(sqlite3_exec(db.handle(), R"sql(
        ALTER TABLE mikan_subscription DROP COLUMN preferred_subtitle;
        ALTER TABLE mikan_download DROP COLUMN subtitle_scripts;
        INSERT INTO mikan_download VALUES('local','old','["進擊的巨人",1,0,"3"]','url1','','',1);
        INSERT INTO mikan_title_alias VALUES('local','old','進擊','進擊的巨人');
        DROP TABLE subscription_collection; DROP VIEW watch_episode_state; DROP VIEW watch_episode; DROP TABLE watch_progress; DROP TABLE watch_sync_queue; DROP TABLE watch_sync_account; DROP TABLE watch_clock; DROP TABLE watch_bangumi_episode;
        DROP TABLE mikan_rule_baseline; DROP TABLE mikan_rule_history;
        PRAGMA user_version=14;
    )sql",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    db.migrate();
    MikanDownloadStore store(db);
    MikanSubscription spec{feed, "", ""};
    MikanCandidate entry{R"(["进击的巨人",1,0,"3"])", "url2", "", "", "", 0, 1};
    REQUIRE_FALSE(store.wanted("local", spec, entry));
    REQUIRE(store.aliases("local", "").at("进击").contains("进击的巨人"));
    spec.subtitle = "cht";
    store.subscribe("local", spec);
    REQUIRE(store.feeds("local").front().subtitle == "cht");
    entry.subtitleScripts = 2;
    entry.score = 1;
    REQUIRE(store.wanted("local", spec, entry));
    entry.title = "[G] 进击的巨人 - 03 [CHT]";
    REQUIRE(store.reserve("local", spec, entry));
    const auto waiting = store.pending("local", spec);
    REQUIRE(waiting.size() == 1);
    REQUIRE(waiting.front().first.score == 1);
    REQUIRE(store.wanted("local", spec, waiting.front().first));
    store.record("local", spec, entry);
    entry.url = "url3";
    entry.subtitleScripts = 1;
    entry.score = 0;
    REQUIRE_FALSE(store.wanted("local", spec, entry));
    store.learnSubtitleScripts("local", "old", {{"url1", 1}});
    REQUIRE(sqlite3_changes(db.handle()) == 1);
}
TEST_CASE("A numeric title suffix before a dash is a release, not a range") {
    auto selected = selectMikanDownloads(
        {feed, "", ""},
        items({article("[G] Mob Psycho 100 - 03 [1080p]", 1), article("[G] Other Season 2 - 03 [1080p]", 2),
               article("[G] Other - 01-12 [1080p]", 3), article("[Different] Other S02E03 [720p]", 4)}),
        "[]");
    REQUIRE(selected.size() == 2);
}
TEST_CASE("Managed RSS endpoint retries rejected additions and records successful episodes") {
    const auto dir = std::filesystem::temp_directory_path() / ("av-mikan-" + std::to_string(std::random_device{}()));
    SqliteDatabase db(":memory:");
    db.migrate();
    constexpr int port = 18889;
    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    auto manager = std::make_shared<QbConnectionManager>(dir, QbConnectionConfig{base, "test", "pass"});
    auto worker = std::make_shared<MikanDownloadWorker>(db, manager, dir.string(), [] {});
    api::registerQbRssEndpoints(manager, dir, [] { return std::string{}; }, [] {}, worker);
    std::atomic_int submissions{};
    std::atomic_bool rotated{}, confirmed{};
    std::string assignedTag;
    std::mutex tagMutex;
    const auto mock = [](const std::string &path, auto handler) {
        drogon::app().registerHandler(
            path,
            [handler](const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&cb) {
                auto response = drogon::HttpResponse::newHttpResponse();
                response->setBody(handler(req, response));
                cb(response);
            },
            {drogon::Get, drogon::Post});
    };
    mock("/", [](auto, auto) { return "qBittorrent WebUI"; });
    mock("/api/v2/auth/login", [](auto, auto response) {
        response->addCookie("SID", "mock");
        return "Ok.";
    });
    mock("/api/v2/app/preferences",
         [](auto, auto) { return R"({"rss_processing_enabled":true,"rss_auto_downloading_enabled":true})"; });
    const std::string legacyName = "AnimeVault Auto 3bb54568d17b2812";
    const std::string single = "https://mikanani.me/RSS/Bangumi?bangumiId=42";
    const std::string singleName = "AnimeVault Auto d3ef25aa2164189b";
    std::atomic_bool nativeEnabled{true}, customTags{true}, singleEnabled{true};
    mock("/api/v2/rss/rules", [&](auto, auto) {
        auto definition =
            Json::parse(QbRssRules::autoMikanRuleJson({legacyName, feed, "", dir.string()}, nativeEnabled));
        definition["torrentParams"] = {{"save_path", dir.string()},
                                       {"tags", customTags.load() ? Json::array({"custom"}) : Json::array()},
                                       {"ratio_limit", -2},
                                       {"operating_mode", "AutoManaged"}};
        auto safe = Json::parse(QbRssRules::autoMikanRuleJson({singleName, single, "", dir.string()}, singleEnabled));
        safe["torrentParams"] = {{"seed_mode", false}, {"share_limits_mode", "Default"}};
        safe["previouslyMatchedEpisodes"] = {"3", "7"};
        auto custom = safe;
        custom["affectedFeeds"] = {single + "&subgroupid=12"};
        custom["assignedCategory"] = "My filter";
        custom["enabled"] = true;
        return Json{{legacyName, definition}, {singleName, safe}, {"custom rule", custom}}.dump();
    });
    mock("/api/v2/rss/setRule", [&](auto req, auto response) {
        auto updated = Json::parse(req->getParameter("ruleDef"));
        if (req->getParameter("ruleName") == singleName && !updated.value("enabled", true) &&
            updated["previouslyMatchedEpisodes"] == Json::array({"3", "7"})) {
            singleEnabled = false;
            response->setStatusCode(drogon::k204NoContent);
            return "";
        }
        if (req->getParameter("ruleName") != legacyName || updated.value("enabled", true))
            response->setStatusCode(drogon::k400BadRequest);
        else
            nativeEnabled = false;
        return "";
    });
    mock("/api/v2/torrents/info", [&](auto, auto) {
        std::lock_guard lock(tagMutex);
        return confirmed.load() ? Json::array({{{"name", "renamed file.mkv"}, {"tags", assignedTag}}}).dump()
                                : std::string("[]");
    });
    mock("/api/v2/rss/items", [&](auto req, auto) {
        if (req->getParameter("withData") != "true")
            return std::string("{}");
        auto data = Json::parse(
            rotated ? items(Json::array())
                    : items({article("[Team] Show S01E03 [1080p]", 1), article("[Other] Show S01E03 [720p]", 2)}));
        data["single"] = {{"title", "Mikan Project - Example"},
                          {"url", single},
                          {"articles", {article("[Other] Different Name - 03 [720p]", 3)}}};
        data["another"] = {{"url", single + "&subgroupid=999"}, {"articles", Json::array()}};
        return data.dump();
    });
    mock("/api/v2/torrents/add", [&](auto req, auto) {
        if (req->getParameter("urls") != "https://mikanani.me/Download/1.torrent" ||
            req->getParameter("savepath") != dir.string()) {
            std::cerr << "fixture unexpected form: " << req->getBody() << " parsed URL " << req->getParameter("urls")
                      << '\n';
            return "Fails.";
        }
        {
            std::lock_guard lock(tagMutex);
            assignedTag = req->getParameter("tags");
        }
        return ++submissions == 1 ? "Fails." : R"({"pending_count":1,"success_count":0})";
    });
    drogon::app().addListener("127.0.0.1", port);
    std::thread server([] { drogon::app().run(); });
    struct Shutdown {
        std::thread &thread;
        MikanDownloadWorker &worker;
        ~Shutdown() {
            worker.stop();
            drogon::app().quit();
            thread.join();
        }
    } stop{server, *worker};
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto http = drogon::HttpClient::newHttpClient(base);
    REQUIRE(sqlite3_exec(db.handle(), "UPDATE watch_sync_account SET account=11,enabled=1,connected=1", nullptr,
                         nullptr, nullptr) == SQLITE_OK);
    auto enroll = [&](std::string address = feed, bool foreign = false) {
        ::Json::Value body;
        body["url"] = address;
        body["preferredGroup"] = "Team";
        body["preferredResolution"] = "1080p";
        auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/api/qb/rss/feeds");
        if (foreign)
            req->addHeader("Origin", "https://untrusted.example");
        const auto [status, response] = http->sendRequest(req, 8.0);
        REQUIRE(status == drogon::ReqResult::Ok);
        REQUIRE(response);
        return response->getJsonObject();
    };
    REQUIRE((*enroll())["errorCode"].asString() == "qb_rule_exists");
    REQUIRE(nativeEnabled.load());
    customTags = false;
    auto failed = enroll();
    REQUIRE_FALSE(nativeEnabled.load());
    REQUIRE((*failed)["errorCode"].asString() == "qb_torrent_rejected");
    auto retried = enroll();
    INFO(retried->toStyledString());
    INFO(submissions.load());
    REQUIRE((*retried)["success"].asBool());
    MikanDownloadStore store(db);
    MikanSubscription spec{feed, "Team", "1080p"};
    const auto waiting = store.pending(base, spec);
    REQUIRE(waiting.size() == 1);
    {
        std::lock_guard lock(tagMutex);
        REQUIRE(assignedTag.starts_with("AnimeVault-"));
    }
    rotated = true;
    store.retry(base, waiting.front().first, 0);
    REQUIRE((*enroll())["success"].asBool());
    REQUIRE(submissions.load() == 3);
    confirmed = true;
    REQUIRE((*enroll())["success"].asBool());
    REQUIRE(submissions.load() == 3);
    REQUIRE(store.pending(base, {feed, "Team", "1080p"}).empty());
    auto sync = [&](bool preview) {
        ::Json::Value body;
        body["preview"] = preview;
        auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/api/qb/rss/sync");
        auto [status, response] = http->sendRequest(req, 8.0);
        REQUIRE(status == drogon::ReqResult::Ok);
        REQUIRE(response->statusCode() == drogon::k200OK);
        return *response->getJsonObject();
    };
    auto plan = sync(true);
    REQUIRE(plan["eligible"].asInt() == 1);
    REQUIRE(plan["retained"].asInt() == 1);
    REQUIRE(plan["baselineArticles"].asInt() == 1);
    REQUIRE(plan["historicalEpisodes"].asInt() == 2);
    REQUIRE(singleEnabled.load());
    REQUIRE(store.baseline(base, single).empty());
    REQUIRE(sync(false)["migrated"].asInt() == 1);
    REQUIRE_FALSE(singleEnabled.load());
    REQUIRE(store.baseline(base, single).size() == 1);
    REQUIRE_FALSE(store.wanted(base, {single}, {R"(["example",1,0,"7"])", "new", "title"}));
    REQUIRE(store.wanted(base, {single}, {R"(["example",1,0,"8"])", "new", "title"}));
    auto again = sync(true);
    REQUIRE(again["eligible"].asInt() == 0);
    REQUIRE(again["alreadyManaged"].asInt() == 1);
    REQUIRE(again["retained"].asInt() == 1);
    REQUIRE(submissions.load() == 3);
    const auto another = single + "&subgroupid=999";
    REQUIRE((*enroll(another, true))["error"]["code"].asString() == "rss_origin_forbidden");
    REQUIRE((*enroll(another))["success"].asBool());
    REQUIRE((*enroll(another))["success"].asBool());
    sqlite3_stmt *check = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT account,mikan_id,ready FROM subscription_collection", -1, &check,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(check) == SQLITE_ROW);
    CHECK(sqlite3_column_int(check, 0) == 11);
    CHECK(sqlite3_column_int(check, 1) == 42);
    CHECK(sqlite3_column_int(check, 2) == 1);
    CHECK(sqlite3_step(check) == SQLITE_DONE);
    sqlite3_finalize(check);
}
} // namespace
