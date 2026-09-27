# Phase 4 Bangumi Matching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add direct Bangumi subject search, ranked candidates, local caching and retry state, and explicit local binding without making scans or organization depend on the network.

**Architecture:** Keep title normalization and candidate scoring in a pure service. An asynchronous transport port fetches Bangumi's public `/v0/` API; a SQLite-backed cache stores response JSON and retry metadata. The HTTP controller validates requests and maps stable errors without blocking Drogon's event loop, while local anime/media records remain usable when Bangumi is offline.

**Tech Stack:** C++20, Drogon HTTP client, SQLite, Catch2, Bangumi public API v0.

---

## Preconditions and source contract

- Read `AGENTS.md`, the approved phase 0–5 design, `MediaRepository`, `SqliteMediaRepository`, `MediaController`, and completed Phase 3 result before code changes. Do not begin until the Phase 3 gate is honestly reported.
- Official API: `POST https://api.bgm.tv/v0/search/subjects?limit=20` with JSON `{ "keyword": "...", "filter": { "type": [2] } }`; response `data` is an array of subjects. `GET /v0/subjects/{id}` provides a subject by ID. Search is marked experimental, so parse defensively. Sources: https://github.com/bangumi/api/blob/master/open-api/v0.yaml and https://github.com/bangumi/dev-docs/blob/master/README.md.
- Identify this non-browser client with a configurable developer/app User-Agent; no default HTTP-library User-Agent. Source: https://github.com/bangumi/api/blob/master/docs-raw/user%20agent.md. Use direct HTTPS, never the qB proxy bridge or its ports.
- No live Bangumi call in tests. Use one focused fake-transport test for ranking/cache/failure and one repository test for binding. Do not touch `D:\追番` in tests.
- Search never creates or changes a binding. An explicit binding request may update it; a unique high-confidence candidate can be reported as eligible for future automatic binding, but ambiguity must never silently bind.

Use this service boundary (extend with only fields actually required by the UI):

```cpp
struct BangumiCandidate {
    std::int64_t id{};
    std::string name, nameCn, date, coverUrl;
    int episodeCount{}, type{};
    double score{};
};
struct RankedCandidates {
    std::vector<BangumiCandidate> items;
    bool autoBindEligible{};
};
struct BangumiSubject {
    std::int64_t id{};
    std::string name, nameCn, date, coverUrl;
    int episodeCount{}, type{};
};
struct BangumiMatchQuery {
    std::string title;
    std::optional<int> year;
    std::optional<int> episodeCount;
};
RankedCandidates rankBangumiCandidates(const BangumiMatchQuery& query,
                                       const std::vector<BangumiSubject>& subjects);
class BangumiTransport {
public:
    virtual ~BangumiTransport() = default;
    struct Response { int status{}; std::string body; };
    using Completion = std::function<void(std::optional<Response>, std::string)>;
    virtual void search(std::string keyword, Completion completion) = 0;
    virtual void subject(std::int64_t id, Completion completion) = 0;
};
```

## File map

- Create `backend/include/anime_vault/services/BangumiMatcher.hpp` and `backend/src/services/BangumiMatcher.cpp`: normalized-title score, deterministic top-five ordering, conservative eligibility flag.
- Create `backend/include/anime_vault/ports/BangumiTransport.hpp`: search/get-subject transport interface and typed transport failure.
- Create `backend/include/anime_vault/services/BangumiService.hpp` and `backend/src/services/BangumiService.cpp`: input bounds, in-process rate limit, cache and retry orchestration.
- Create `backend/include/anime_vault/infrastructure/network/DrogonBangumiTransport.hpp` and `backend/src/infrastructure/network/DrogonBangumiTransport.cpp`: direct HTTPS request, timeout, User-Agent, bounded JSON parsing.
- Extend `MediaRepository` and `SqliteMediaRepository`: cache read/write, anime list/detail, explicit bind and aliases. Add ordered migration 006 only if a necessary field or index is absent; never rewrite migrations 001–005.
- Extend `MediaController` and `main.cpp`: `GET /api/bangumi/search?q=`, `GET /api/anime`, `GET /api/anime/{id}`, `PUT /api/anime/{id}/bangumi`.
- Add `backend/tests/unit/BangumiMatcherTest.cpp` and `backend/tests/integration/BangumiServiceTest.cpp` using fake transport and disposable SQLite.

## Task 1: Pure candidate scoring

**Files:** Create `BangumiMatcher.hpp/.cpp`, add target to `backend/CMakeLists.txt`, create `BangumiMatcherTest.cpp`, register focused CTest in `backend/tests/CMakeLists.txt`.

- [ ] Write a failing test with the query `葬送的芙莉莲`, one exact animation title, one alternate-title match, one unrelated title, and two equally strong candidates. Assert descending deterministic score, maximum five returned, animation-only candidates, and `autoBindEligible == false` for a tie.
- [ ] Run `cmake --build build/test --config Debug --target anime_vault_bangumi_matcher_tests` then `ctest --test-dir build/test -C Debug -R BangumiMatcher --output-on-failure`; RED must be missing matcher symbols, not a missing dependency. If the full build is unavailable, use a standalone MSVC/Catch2 compile and record the precise command.
- [ ] Define the three DTOs and `rankBangumiCandidates` signature exactly as above. Implement normalized case/space/punctuation-insensitive title comparison without network or database access; exact primary/Chinese title dominates fuzzy similarity. If supplied, matching year and episode count provide small bonuses but cannot rescue an unrelated title. Clamp score to `[0,1]`; eligibility requires a unique first candidate `>=0.92` with a margin `>=0.08` over second.
- [ ] Re-run focused test; GREEN must include tie and unrelated cases. Commit `feat: rank Bangumi animation candidates conservatively`.

## Task 2: Cached search with offline behavior

**Files:** Create `BangumiTransport.hpp`, `BangumiService.hpp/.cpp`, `DrogonBangumiTransport.hpp/.cpp`; extend repository cache methods; add `BangumiServiceTest.cpp`, CMake targets.

- [ ] Write a failing fake-transport test: first asynchronous search stores response, second identical normalized query uses cache without another call, transport failure invokes the callback with `bangumi_unavailable` and retry metadata while leaving local repository usable, and an expired cache makes one new call. Use a disposable SQLite DB and a fake clock/transport; never contact Bangumi.
- [ ] Run only `BangumiServiceTest` and verify RED is the missing service/port.
- [ ] Use a bounded query (`1..100` UTF-8 bytes after trimming), cache key built as `"search:v0:" + normalize(query)`, TTL 24 hours, `retry_count` increment on failure, and capped next retry delay (for example 1, 2, 4, 8, 16 minutes). Parse cached JSON through the same validated subject parser as live responses; reject malformed objects, oversized bodies, nonpositive IDs, and missing names with `bangumi_bad_response` without exposing the response body.
- [ ] Transport sends the documented v0 POST with animation type `2`, `limit=20`, `Content-Type: application/json`, a configured identifiable User-Agent, 5-second timeout, direct HTTPS, certificate validation, and no proxy configuration. Call Drogon's asynchronous `sendRequest(request, completion, 5.0)`; do not call its synchronous overload from an HTTP handler because that can deadlock the event loop. Callback state must own its data by value or shared ownership, never capture request-stack references. 429/5xx/timeouts become retryable `bangumi_unavailable`; other non-200 responses become stable sanitized errors. Bound response body bytes before parsing and never log token, body, or private URL. Drogon client contract: https://github.com/drogonframework/drogon/blob/master/lib/inc/drogon/HttpClient.h.
- [ ] Re-run focused test. Commit `feat: search Bangumi with cache and bounded retry`.

## Task 3: Local binding and read API

**Files:** Extend repository port/SQLite adapter, `MediaController.cpp`, `MediaController.hpp` if needed, and `main.cpp`; add one disposable database contract test.

- [ ] Write a failing test: explicitly binding anime ID A to positive Bangumi subject ID B updates only A, stores the displayed metadata and an alias, and a second anime cannot acquire B. Invalid IDs and a lock conflict return stable errors. Listing/detail works from SQLite with the transport offline.
- [ ] Run only the binding contract test and verify RED.
- [ ] Add repository transaction `bindAnime(animeId, subject)` that checks the target anime exists, rejects `locked=1` unless its existing binding is unchanged, enforces one anime per Bangumi subject, updates subject/name/cover/year conservatively, and inserts normalized aliases. Add a unique index in migration 006 if the current schema does not enforce unique non-null `bangumi_subject_id`; update embedded SQL and schema version together.
- [ ] Expose `GET /api/anime`, `GET /api/anime/{id}`, `GET /api/bangumi/search?q=`, and `PUT /api/anime/{id}/bangumi`. The PUT DTO accepts only a positive `subjectId` and explicit confirmation, not arbitrary remote JSON; fetch the subject asynchronously through the service, validate type animation, then bind. Complete each Drogon HTTP callback exactly once; the two local GET endpoints do no network I/O. Apply existing request-ID/error conventions and bound response sizes.
- [ ] Re-run focused test and, if Drogon builds, one route-level validation assertion. Commit `feat: expose local anime and explicit Bangumi binding`.

## Task 4: Prefer unambiguous local bindings and aliases

The approved phase 0–5 design requires local manual bindings and normalized aliases to be checked before a Bangumi request. This task closes that integration gap after Task 3; it must not silently bind ambiguous names.

- [ ] Write a failing disposable-SQLite/fake-transport test: a previously bound anime's title or Bangumi alias resolves locally without a network call; a new scanned/corrected title matching exactly one alias links to that local anime; two anime sharing an alias do not auto-link, and a search may fall back to remote candidates.
- [ ] Use one bounded, deterministic title normalization for both stored aliases and lookup; preserve compatibility with aliases already stored by migrations 001–005. An existing explicit media-to-anime binding takes precedence over title lookup. Never select an ambiguous alias by row order.
- [ ] Return an explicit local-match indicator to the search API/UI, without treating a mere high score as a confirmed binding. Keep direct Bangumi network I/O only for an unresolved lookup, and preserve the offline local list/detail/scan/organize behavior.
- [ ] Run focused tests and commit; then run the milestone gate below.

## Milestone gate

- [ ] Review all changed files against the approved design: Bangumi direct Internet only; no scan/organize dependency on network; no silent ambiguous binding; no sensitive logging.
- [ ] Run CMake configure/build, focused CTest, frontend test/lint/build; report exact unavailable prerequisites rather than marking unrun tests passed.
- [ ] Verify transport-offline search failure leaves local list/detail, scan and organization available on disposable data. Do not perform a live Bangumi call as an automated test.
