#include "anime_vault/infrastructure/network/BangumiConnectionManager.hpp"
#include "anime_vault/infrastructure/network/DrogonBangumiTransport.hpp"
#include "anime_vault/infrastructure/network/DrogonCoverTransport.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <algorithm>
#include <memory>

namespace anime_vault {
namespace {
constexpr const char* kSettingKey = "bangumiUserAgent";
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement statement(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK)
        throw BangumiConfigError("bangumi_config_storage_failed", "cannot prepare Bangumi setting");
    return Statement(raw, sqlite3_finalize);
}
}

bool BangumiConnectionManager::validUserAgent(const std::string& value) {
    return !value.empty() && value.size() <= 200 &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch >= 0x20 && ch != 0x7f; });
}

std::shared_ptr<BangumiConnectionManager::Active> BangumiConnectionManager::makeActive(const std::string& agent) {
    if (agent.empty()) return {};
    auto active = std::make_shared<Active>();
    active->api = std::make_shared<DrogonBangumiTransport>(agent);
    active->covers = std::make_shared<DrogonCoverTransport>(agent);
    return active;
}

std::string BangumiConnectionManager::loadSaved() const {
    std::lock_guard lock(database_.mutex());
    auto stmt = statement(database_.handle(), "SELECT value_json FROM setting WHERE key=?");
    sqlite3_bind_text(stmt.get(), 1, kSettingKey, -1, SQLITE_STATIC);
    const int result = sqlite3_step(stmt.get());
    if (result == SQLITE_DONE) return {};
    if (result != SQLITE_ROW) throw BangumiConfigError("bangumi_config_storage_failed", "cannot read Bangumi setting");
    const auto* raw = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    try {
        auto value = nlohmann::json::parse(raw ? raw : "");
        if (!value.is_string() || !validUserAgent(value.get<std::string>())) throw std::invalid_argument("invalid");
        return value.get<std::string>();
    } catch (...) { throw BangumiConfigError("bangumi_config_corrupt", "invalid saved Bangumi setting"); }
}

BangumiConnectionManager::BangumiConnectionManager(SqliteDatabase& database, std::string environmentUserAgent)
    : database_(database), environmentUserAgent_(std::move(environmentUserAgent)) {
    if (!environmentUserAgent_.empty() && !validUserAgent(environmentUserAgent_))
        throw BangumiConfigError("invalid_user_agent", "invalid environment Bangumi User-Agent");
    const auto saved = loadSaved();
    const auto agent = saved.empty() ? environmentUserAgent_ : saved;
    summary_ = {agent, saved.empty() ? (agent.empty() ? "none" : "environment") : "saved", !agent.empty()};
    active_ = makeActive(agent);
}

BangumiConfigSummary BangumiConnectionManager::summary() const {
    std::lock_guard lock(mutex_);
    return summary_;
}

void BangumiConnectionManager::save(std::string agent) {
    if (!validUserAgent(agent)) throw BangumiConfigError("invalid_user_agent", "invalid Bangumi User-Agent");
    auto next = makeActive(agent);
    const auto encoded = nlohmann::json(agent).dump();
    std::lock_guard lock(mutex_);
    {
        std::lock_guard dbLock(database_.mutex());
        auto stmt = statement(database_.handle(), "INSERT INTO setting(key,value_json) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value_json=excluded.value_json");
        sqlite3_bind_text(stmt.get(), 1, kSettingKey, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt.get(), 2, encoded.c_str(), static_cast<int>(encoded.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE)
            throw BangumiConfigError("bangumi_config_storage_failed", "cannot save Bangumi setting");
    }
    // The service keeps this adapter alive; each request retains its transport snapshot.
    summary_ = {std::move(agent), "saved", true};
    active_ = std::move(next);
}

void BangumiConnectionManager::clear() {
    auto next = makeActive(environmentUserAgent_);
    std::lock_guard lock(mutex_);
    {
        std::lock_guard dbLock(database_.mutex());
        auto stmt = statement(database_.handle(), "DELETE FROM setting WHERE key=?");
        sqlite3_bind_text(stmt.get(), 1, kSettingKey, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE)
            throw BangumiConfigError("bangumi_config_storage_failed", "cannot clear Bangumi setting");
    }
    summary_ = {environmentUserAgent_, environmentUserAgent_.empty() ? "none" : "environment", !environmentUserAgent_.empty()};
    active_ = std::move(next);
}

void BangumiConnectionManager::search(std::string keyword, BangumiTransport::Completion completion) {
    std::shared_ptr<Active> snapshot;
    { std::lock_guard lock(mutex_); snapshot = active_; }
    if (!snapshot) { completion(std::nullopt, "bangumi_unconfigured"); return; }
    snapshot->api->search(std::move(keyword), [snapshot, completion = std::move(completion)](auto response, auto error) mutable {
        completion(std::move(response), std::move(error));
    });
}

void BangumiConnectionManager::subject(std::int64_t id, BangumiTransport::Completion completion) {
    std::shared_ptr<Active> snapshot;
    { std::lock_guard lock(mutex_); snapshot = active_; }
    if (!snapshot) { completion(std::nullopt, "bangumi_unconfigured"); return; }
    snapshot->api->subject(id, [snapshot, completion = std::move(completion)](auto response, auto error) mutable {
        completion(std::move(response), std::move(error));
    });
}

void BangumiConnectionManager::fetch(std::string url, CoverImageFetcher::Completion completion) {
    std::shared_ptr<Active> snapshot;
    { std::lock_guard lock(mutex_); snapshot = active_; }
    if (!snapshot) { completion(std::nullopt, "bangumi_unconfigured"); return; }
    snapshot->covers->fetch(std::move(url), [snapshot, completion = std::move(completion)](auto response, auto error) mutable {
        completion(std::move(response), std::move(error));
    });
}
}
