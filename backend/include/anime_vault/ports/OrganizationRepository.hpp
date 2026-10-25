#pragma once

#include "anime_vault/domain/MediaRecords.hpp"

namespace anime_vault {

class OrganizationRepository {
public:
    virtual ~OrganizationRepository() = default;
    virtual std::int64_t insertPlan(const PlanRecord& record) = 0;
    virtual std::optional<PlanRecord> getPlan(std::int64_t id) const = 0;
    virtual std::optional<PlanRecord> findPlanByIdempotencyKey(const std::string& key) const = 0;
    virtual OrganizationJobRecord claimOrganization(std::int64_t planId, const std::string& key) = 0;
    virtual std::optional<OrganizationJobRecord> findOrganizationJobByKey(const std::string& key) const = 0;
    virtual std::optional<OrganizationJobRecord> getOrganizationJob(std::int64_t id) const = 0;
    virtual void completeOrganization(std::int64_t jobId, const std::string& resultPath,
                                      std::int64_t bytes) = 0;
    virtual void failOrganization(std::int64_t jobId, const std::string& code) = 0;
};

} // namespace anime_vault
