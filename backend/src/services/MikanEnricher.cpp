#include "anime_vault/services/MikanEnricher.hpp"
#include "anime_vault/ports/AnimeRepository.hpp"

#include <utility>
#include <algorithm>
#include <regex>

namespace anime_vault {
MikanEnricher::MikanEnricher(AnimeRepository& repository, Fetcher fetcher)
    : repository_(repository), fetcher_(std::move(fetcher)) {}

void MikanEnricher::runOnce(Completion completion) const {
    try {
        fetcher_([this, completion = std::move(completion)](MikanCatalog catalog) mutable {
            MikanEnrichmentResult result;
            result.errorCode = std::move(catalog.errorCode);
            if (result.errorCode.empty()) {
                try {
                    // 首轮可能由后面的别名认出本地番剧；第二轮补齐同文中的其他名称。
                    for (int pass = 0; pass < 2; ++pass)
                        for (const auto& pair : catalog.pairs) {
                            if (pair.feedScoped) {
                                auto candidate = repository_.findAnimeByTitle(pair.alias);
                                if (!candidate) candidate = repository_.findAnimeByTitle(pair.canonicalTitle);
                                if (!candidate || pair.episode <= 0) continue;
                                const auto detail = repository_.getAnime(candidate->id);
                                if (!detail) continue;
                                // 单番 feed 的季数证据必须落在本地同一原始集数，防止第三季订阅改写第一季。
                                const auto raw = std::to_string(pair.episode);
                                if (std::none_of(detail->media.begin(), detail->media.end(), [&](const auto& media) {
                                    return media.episodeType == "normal" && media.rawEpisodeNumber == raw;
                                })) continue;
                                static const std::regex season(R"(第(一|二|三|四|五|六|七|八|九|[0-9]+)季)");
                                std::smatch match;
                                if (!detail->season.empty() && std::regex_search(pair.canonicalTitle, match, season)) {
                                    std::string expected = match[1].str();
                                    const std::vector<std::string> numbers{"一","二","三","四","五","六","七","八","九"};
                                    const auto found = std::find(numbers.begin(), numbers.end(), expected);
                                    if (found != numbers.end()) expected = std::to_string(found - numbers.begin() + 1);
                                    if (detail->season != expected) continue;
                                }
                            }
                            if (repository_.applyMikanAlias(pair.alias, pair.canonicalTitle)) ++result.appliedCount;
                        }
                } catch (...) { result.errorCode = "mikan_storage_error"; }
            }
            if (completion) completion(std::move(result));
        });
    } catch (...) {
        if (completion) completion({0, "mikan_fetch_error"});
    }
}
}
