#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <regex>

#include "SqliteSupport.hpp"
#include "SqliteRepositoryRows.hpp"

namespace anime_vault {
using namespace sqlite_support;
using namespace sqlite_rows;

std::int64_t SqliteMediaRepository::insertPlan(const PlanRecord& record) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "INSERT INTO organize_plan(media_file_id,source_path,source_size,source_modified_at,target_path,operation,expires_at,execution_state,idempotency_key) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(idempotency_key) DO NOTHING");
    sqlite3_bind_int64(stmt.get(), 1, record.mediaFileId);
    sqlite_support::bind(stmt.get(), 2, record.sourcePath);
    sqlite3_bind_int64(stmt.get(), 3, record.sourceSize);
    sqlite_support::bind(stmt.get(), 4, record.sourceModifiedAt); sqlite_support::bind(stmt.get(), 5, record.targetPath);
    sqlite_support::bind(stmt.get(), 6, record.operation); sqlite_support::bind(stmt.get(), 7, record.expiresAt);
    sqlite_support::bind(stmt.get(), 8, record.executionState); sqlite_support::bind(stmt.get(), 9, record.idempotencyKey);
    done(db, stmt.get());
    const auto plan = findPlanByIdempotencyKey(record.idempotencyKey);
    if (!plan) throw std::runtime_error("plan insert failed");
    return plan->id;
}

std::optional<PlanRecord> SqliteMediaRepository::getPlan(std::int64_t id) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,media_file_id,source_path,source_size,source_modified_at,target_path,operation,expires_at,execution_state,idempotency_key FROM organize_plan WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return planRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}

std::optional<PlanRecord> SqliteMediaRepository::findPlanByIdempotencyKey(const std::string& key) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,media_file_id,source_path,source_size,source_modified_at,target_path,operation,expires_at,execution_state,idempotency_key FROM organize_plan WHERE idempotency_key=?");
    sqlite_support::bind(stmt.get(), 1, key);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return planRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}

OrganizationJobRecord SqliteMediaRepository::claimOrganization(std::int64_t planId,
                                                               const std::string& key) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    if (const auto previous = findJobByKey(db, key)) {
        if (previous->planId != planId) throw OrganizationClaimError("idempotency_key_reused");
        tx.commit();
        return *previous;
    }
    auto update = prepare(db, "UPDATE organize_plan SET execution_state='claimed',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND execution_state='pending' AND expires_at>strftime('%Y-%m-%dT%H:%M:%SZ','now') "
        "AND EXISTS (SELECT 1 FROM media_file m WHERE m.id=organize_plan.media_file_id AND m.library_path IS NULL) "
        "AND NOT EXISTS (SELECT 1 FROM organize_plan p JOIN organize_job j ON j.plan_id=p.id "
        "WHERE p.media_file_id=organize_plan.media_file_id AND j.status IN ('running','completed'))");
    sqlite3_bind_int64(update.get(), 1, planId);
    done(db, update.get());
    if (sqlite3_changes(db) != 1) {
        auto plan = prepare(db, "SELECT execution_state,expires_at<=strftime('%Y-%m-%dT%H:%M:%SZ','now') "
            "FROM organize_plan WHERE id=?");
        sqlite3_bind_int64(plan.get(), 1, planId);
        if (sqlite3_step(plan.get()) != SQLITE_ROW) throw OrganizationClaimError("plan_not_found");
        const auto state = column(plan.get(), 0);
        const bool expired = sqlite3_column_int(plan.get(), 1) != 0;
        if (state.rfind("conflict:", 0) == 0) throw OrganizationClaimError("plan_conflict");
        if (state != "pending") throw OrganizationClaimError("plan_already_claimed");
        if (expired) throw OrganizationClaimError("plan_expired");
        throw OrganizationClaimError("plan_already_claimed");
    }
    auto insert = prepare(db, "INSERT INTO organize_job(plan_id,status,idempotency_key) VALUES(?,'running',?)");
    sqlite3_bind_int64(insert.get(), 1, planId);
    sqlite_support::bind(insert.get(), 2, key);
    done(db, insert.get());
    const auto jobId = sqlite3_last_insert_rowid(db);
    audit(db, jobId, "organization_claimed", "claimed");
    tx.commit();
    return {jobId, planId, key, "running", "", "", 0, true};
}

std::optional<OrganizationJobRecord> SqliteMediaRepository::findOrganizationJobByKey(
    const std::string& key) const {
    std::lock_guard lock(database_.mutex());
    return findJobByKey(database_.handle(), key);
}

std::optional<OrganizationJobRecord> SqliteMediaRepository::getOrganizationJob(std::int64_t id) const {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    auto stmt = prepare(db, "SELECT id,plan_id,idempotency_key,status,result_path,failure_details,result_bytes FROM organize_job WHERE id=?");
    sqlite3_bind_int64(stmt.get(), 1, id);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) return jobRow(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error(sqlite3_errmsg(db));
}

void SqliteMediaRepository::completeOrganization(std::int64_t jobId,
                                                 const std::string& resultPath,
                                                 std::int64_t bytes) {
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Transaction tx(db);
    auto media = prepare(db, "UPDATE media_file SET library_path=?,link_mode=(SELECT operation FROM organize_plan p "
        "JOIN organize_job j ON j.plan_id=p.id WHERE j.id=?),status='organized',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=(SELECT p.media_file_id FROM organize_plan p JOIN organize_job j ON j.plan_id=p.id WHERE j.id=? "
        "AND j.status='running') AND library_path IS NULL");
    sqlite_support::bind(media.get(), 1, resultPath);
    sqlite3_bind_int64(media.get(), 2, jobId);
    sqlite3_bind_int64(media.get(), 3, jobId);
    done(db, media.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    auto job = prepare(db, "UPDATE organize_job SET status='completed',result_path=?,result_bytes=?,updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND status='running'");
    sqlite_support::bind(job.get(), 1, resultPath);
    sqlite3_bind_int64(job.get(), 2, bytes);
    sqlite3_bind_int64(job.get(), 3, jobId);
    done(db, job.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    auto plan = prepare(db, "UPDATE organize_plan SET execution_state='completed',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=(SELECT plan_id FROM organize_job WHERE id=?) AND execution_state='claimed'");
    sqlite3_bind_int64(plan.get(), 1, jobId);
    done(db, plan.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    audit(db, jobId, "organization_completed", "completed");
    tx.commit();
}

void SqliteMediaRepository::failOrganization(std::int64_t jobId, const std::string& code) {
    std::lock_guard lock(database_.mutex());
    if (code.empty() || code.size() > 64 ||
        !std::all_of(code.begin(), code.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
        })) throw std::invalid_argument("invalid failure code");
    auto* db = database_.handle();
    Transaction tx(db);
    auto job = prepare(db, "UPDATE organize_job SET status='failed',failure_details=?,updated_at=CURRENT_TIMESTAMP "
        "WHERE id=? AND status='running'");
    sqlite_support::bind(job.get(), 1, code);
    sqlite3_bind_int64(job.get(), 2, jobId);
    done(db, job.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    auto plan = prepare(db, "UPDATE organize_plan SET execution_state='failed',updated_at=CURRENT_TIMESTAMP "
        "WHERE id=(SELECT plan_id FROM organize_job WHERE id=?) AND execution_state='claimed'");
    sqlite3_bind_int64(plan.get(), 1, jobId);
    done(db, plan.get());
    if (sqlite3_changes(db) != 1) throw OrganizationClaimError("plan_already_claimed");
    audit(db, jobId, "organization_failed", code);
    tx.commit();
}

} // namespace anime_vault
