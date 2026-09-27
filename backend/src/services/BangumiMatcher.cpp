#include "anime_vault/services/BangumiMatcher.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

namespace anime_vault {
namespace {

double titleScore(const std::u32string& query, const std::u32string& title) {
    if (query.empty() || title.empty()) return 0.0;
    if (query == title) return 1.0;
    std::vector<std::size_t> row(title.size() + 1);
    std::size_t longest = 0;
    for (const auto point : query) {
        for (std::size_t j = title.size(); j > 0; --j) {
            row[j] = point == title[j - 1] ? row[j - 1] + 1 : 0;
            longest = std::max(longest, row[j]);
        }
    }
    const double similarity = 2.0 * static_cast<double>(longest) /
                              static_cast<double>(query.size() + title.size());
    return similarity >= 0.55 ? 0.80 * similarity : 0.0;
}

bool matchesYear(std::string_view date, int year) {
    if (year <= 0 || date.size() < 4) return false;
    int parsed = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (date[i] < '0' || date[i] > '9') return false;
        parsed = parsed * 10 + date[i] - '0';
    }
    return parsed == year;
}

bool hasKnownYear(std::string_view date) {
    if (date.size() < 4) return false;
    return std::all_of(date.begin(), date.begin() + 4,
                       [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

RankedCandidates rankBangumiCandidates(const BangumiMatchQuery& query,
                                       const std::vector<BangumiSubject>& subjects) {
    RankedCandidates ranked;
    const auto normalizedQuery = normalizeTitle(query.title);
    if (normalizedQuery.empty()) return ranked;

    for (const auto& subject : subjects) {
        if (subject.type != 2 || subject.id <= 0) continue;
        const double primary = titleScore(normalizedQuery, normalizeTitle(subject.name));
        const double chinese = titleScore(normalizedQuery, normalizeTitle(subject.nameCn));
        double base = 0.0;
        if (primary == 1.0) base = 0.96;
        else if (chinese == 1.0) base = 0.92;
        else base = std::max(primary, chinese);
        if (base == 0.0) continue;

        double score = base;
        if (query.year) {
            if (matchesYear(subject.date, *query.year)) score += 0.02;
            else if (hasKnownYear(subject.date)) score -= 0.08;
        }
        if (query.episodeCount && *query.episodeCount > 0 && subject.episodeCount > 0) {
            if (subject.episodeCount == *query.episodeCount) score += 0.02;
            else score -= 0.08;
        }
        score = std::clamp(score, 0.0, 1.0);
        ranked.items.push_back({subject.id, subject.name, subject.nameCn, subject.date,
                                subject.coverUrl, subject.episodeCount, subject.type, score});
    }
    std::sort(ranked.items.begin(), ranked.items.end(), [](const auto& a, const auto& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.id < b.id;
    });
    if (!ranked.items.empty()) {
        const double runnerUp = ranked.items.size() > 1 ? ranked.items[1].score : 0.0;
        ranked.autoBindEligible = ranked.items[0].score >= 0.92 &&
            ranked.items[0].score - runnerUp + 1e-12 >= 0.08;
    }
    if (ranked.items.size() > 5) ranked.items.resize(5);
    return ranked;
}

}  // namespace anime_vault
