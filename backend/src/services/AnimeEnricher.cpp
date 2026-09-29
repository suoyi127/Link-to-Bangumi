#include "anime_vault/services/AnimeEnricher.hpp"
#include "anime_vault/repositories/MediaRepository.hpp"
#include "anime_vault/services/CoverScraper.hpp"

#include <algorithm>
#include <vector>

namespace anime_vault {
AnimeEnricher::AnimeEnricher(MediaRepository& repository,
                             std::shared_ptr<BangumiService> bangumi,
                             std::shared_ptr<CoverScraper> covers)
    : repository_(repository), bangumi_(std::move(bangumi)), covers_(std::move(covers)) {}

void AnimeEnricher::runOnce(std::size_t limit, Completion completion) {
    if (!bangumi_ || limit == 0) { if (completion) completion(0); return; }
    std::vector<AnimeRecord> pending;
    try {
        std::int64_t offset = 0;
        while (pending.size() < std::min<std::size_t>(limit, 5)) {
            const auto page = repository_.listAnimePage(offset, 100);
            for (const auto& item : page.items) {
                if (!item.bangumiSubjectId && !item.locked && !item.displayTitle.empty())
                    pending.push_back(item);
                if (pending.size() >= std::min<std::size_t>(limit, 5)) break;
            }
            if (!page.nextOffset) break;
            offset = *page.nextOffset;
        }
    } catch (...) { if (completion) completion(0); return; }

    runPending(std::move(pending), std::move(completion));
}

void AnimeEnricher::runForScan(std::int64_t scanId, Completion completion) {
    if (!bangumi_ || scanId <= 0) { if (completion) completion(0); return; }
    std::vector<AnimeRecord> pending;
    try {
        for (const auto& item : repository_.listAnimeForScan(scanId))
            if (!item.bangumiSubjectId && !item.locked && !item.displayTitle.empty())
                pending.push_back(item);
    } catch (...) { if (completion) completion(0); return; }
    runPending(std::move(pending), std::move(completion));
}

void AnimeEnricher::runPending(std::vector<AnimeRecord> pending, Completion completion) {
    struct Batch {
        std::vector<AnimeRecord> items;
        std::size_t index{};
        std::size_t bound{};
        Completion completion;
    };
    auto batch = std::make_shared<Batch>();
    batch->items = std::move(pending);
    batch->completion = std::move(completion);
    auto self = shared_from_this();
    auto advance = std::make_shared<std::function<void()>>();
    *advance = [self, batch, weakAdvance = std::weak_ptr<std::function<void()>>(advance)]() {
        if (batch->index == batch->items.size()) {
            if (batch->completion) batch->completion(batch->bound);
            return;
        }
        const auto next = weakAdvance.lock();
        if (!next) return;
        const auto item = batch->items[batch->index++];
        self->bangumi_->search({item.displayTitle}, [self, batch, next, item](BangumiSearchResult result) {
            const auto bindVerified = [self, batch, next, id = item.id](BangumiSubjectResult subject) {
                if (subject.subject && subject.errorCode.empty()) {
                    try {
                        const auto current = self->repository_.getAnime(id);
                        if (current && !current->bangumiSubjectId && !current->locked) {
                            self->repository_.bindAnime(id, *subject.subject, true);
                            if (self->covers_ && !subject.subject->coverUrl.empty())
                                self->covers_->cacheSubject(id, *subject.subject);
                            ++batch->bound;
                        }
                    } catch (...) { /* 已变化或冲突的绑定交由用户确认。 */ }
                }
                (*next)();
            };
            // 正常标题匹配优先；只有没有可靠结果时才走 Bangumi 别名回退。
            if (result.candidates.autoBindEligible && !result.candidates.items.empty()) {
                self->bangumi_->subject(result.candidates.items.front().id, bindVerified);
            } else if (!result.localMatch) {
                self->bangumi_->aliasSubject(item.displayTitle, bindVerified);
            } else (*next)();
        });
    };
    (*advance)();
}
}
