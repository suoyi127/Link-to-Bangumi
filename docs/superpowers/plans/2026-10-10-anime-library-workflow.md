# Anime library workflow implementation plan

**Goal:** Deliver the four approved anime workflow improvements.

**Architecture:** QbWebClient supplies verified completion paths; MediaService filters snapshots inside configured roots; SqliteMediaRepository owns inbox filtering, transactional edits and global sorting; React exposes metadata editors and month ordering.

**Tech Stack:** C++20, Drogon, SQLite, React, Catch2, Vitest.

- [x] Add completion parsing and immediate scan regression tests to QbWebClientTest.cpp and ScanReconciliationTest.cpp. Build to demonstrate absent APIs; implement QbWebClient::completedPaths and MediaService::createCompletedScan; verify the focused CTest targets.
- [x] Add inbox binding, edit persistence and global month pagination tests to AnimeBindingTest.cpp. Implement consistent count/list predicates and parameterized repository edits and sorting. Preserve original titles as user aliases.
- [x] Add LibraryPage and AnimeDetailPage interaction tests; observe missing editors/sorting before adding API clients, controlled dialogs and month ordering.
- [x] Add HTTP validation checks and episode persistence regressions. Verify immediate folder responses and background completion polling.
- [x] Run `cmake -S . -B build/test`, `cmake --build build/test --config Debug --parallel 4`, `ctest --test-dir build/test -C Debug --output-on-failure`, `npm.cmd --prefix frontend test -- --run`, `npm.cmd --prefix frontend run lint`, `npm.cmd --prefix frontend run build`.
- [x] Review diff and record exact validation outcomes. Leave the user's unrelated CMake edit intact.

## Verification results (2026-10-10)

- `cmake -S . -B build/test`: passed, prerequisites available.
- `cmake --build build/test --config Debug --parallel 4`: passed. Existing MSVC getenv deprecation warnings remain; the introduced shadow warning was removed.
- `ctest --test-dir build/test -C Debug --output-on-failure --timeout 60`: 31/31 targets passed, 20.97 seconds.
- `npm.cmd --prefix frontend test -- --run --maxWorkers=2 --testTimeout=20000`: 14 files, 81/81 tests passed. The first default parallel run hit 5-second timeouts in existing UI tests; the resource-limited rerun passed without implementation changes for those tests.
- `npm.cmd --prefix frontend run lint`: passed.
- `npm.cmd --prefix frontend run build`: passed. Existing chunk-size warning remains (largest chunk approximately 576 kB).
- Isolated HTTP/1.1 fake-qB smoke: checking and unfinished resources excluded; completed media discovered in 10.13 seconds; anime and episode HTTP edits persisted. Fake credentials were cleared. No real media sources were used.
- Read-only review findings covered by regression tests: edited season/title retained for new episodes; automatic and manual binding preserve custom covers; outstanding image downloads cannot replace later edits.
- Existing tests were updated to reflect the approved exclusion of bound resources and asynchronous folder enrichment.
- `git diff --check`: passed; pre-existing one-space backend/CMakeLists.txt edit preserved. Changes remain on `fix/anime-library-workflow` in the shared workspace.
