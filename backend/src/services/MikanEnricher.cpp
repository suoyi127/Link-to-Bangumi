#include "anime_vault/services/MikanEnricher.hpp"
#include "anime_vault/repositories/MediaRepository.hpp"

#include <utility>

namespace anime_vault {
MikanEnricher::MikanEnricher(MediaRepository& repository, Fetcher fetcher)
    : repository_(repository), fetcher_(std::move(fetcher)) {}

void MikanEnricher::runOnce(Completion completion) const {
    try {
        fetcher_([this, completion = std::move(completion)](MikanCatalog catalog) mutable {
            MikanEnrichmentResult result;
            result.errorCode = std::move(catalog.errorCode);
            if (result.errorCode.empty()) {
                try {
                    for (const auto& pair : catalog.pairs)
                        if (repository_.applyMikanAlias(pair.alias, pair.canonicalTitle))
                            ++result.appliedCount;
                } catch (...) { result.errorCode = "mikan_storage_error"; }
            }
            if (completion) completion(std::move(result));
        });
    } catch (...) {
        if (completion) completion({0, "mikan_fetch_error"});
    }
}
}
