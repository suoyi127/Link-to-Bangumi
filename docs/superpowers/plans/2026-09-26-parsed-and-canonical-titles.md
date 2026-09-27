# Parsed and Canonical Titles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the title parsed from the actual downloaded filename as an alias while organizing files under a user-confirmed canonical anime title.

**Architecture:** The filename parser remains the source of parsed identity; a new `parsed_title` column preserves it independently of the editable canonical correction. User-confirmed correction adds the parsed and prior display titles to the existing alias table and updates the anime display title transactionally. Automatic scans resolve aliases without changing canonical metadata. Preview uses the linked anime display title, and the planner emits canonical title plus bracketed episode number.

**Tech Stack:** C++20, Drogon, SQLite migrations, Catch2; React/TypeScript for the inbox display.

---

## Files and boundaries

- `backend/src/services/FilenameParser.cpp` and `backend/tests/unit/FilenameParserTest.cpp`: parse actual filename tokens and normalize a trailing period.
- `backend/migrations/006_parsed_title.sql`, `backend/CMakeLists.txt`, `backend/src/infrastructure/database/SqliteDatabase.cpp`, generated `InitialSql.hpp`, `MediaRepository.hpp`, `MediaController.cpp`, and `SqliteMediaRepository.cpp`: persist/expose parsed title and maintain aliases/canonical title transactionally.
- `backend/src/api/MediaService.cpp`: choose the linked anime's canonical `displayTitle` for target preview.
- `backend/src/services/TargetPlanner.cpp` and `backend/tests/unit/TargetPlannerTest.cpp`: generate `Canonical Title [01].mkv`, preserving special-type prefixes inside brackets.
- `backend/tests/integration/OrganizationHttpTest.cpp`, migration/repository integration tests, and `frontend/src/components/InboxPage.tsx` tests: prove migration, alias mapping, and user-visible identification/correction.
- `README.md`: document how actual filenames, aliases, canonical names, and target naming relate.

## Task 1: Actual filename parsing

- [ ] Add a parser test for `[Sakurato] Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi o Shita. [01][AVC-8bit 1080P AAC][CHS].mkv`, asserting the release group, title without trailing period, episode 1, and resolution.
- [ ] Run `cmake --build build/ascii-gate --config Debug --target anime_vault_filename_parser_tests` and its matching CTest; confirm the title assertion fails because the trailing separator remains.
- [ ] Strip only trailing filename separators from reconstructed titles; retain periods that occur inside a title. Rerun the focused test and parser fixtures.

## Task 2: Persist parsed title and map aliases

- [ ] Add disposable SQLite tests showing migration 6 backfills `parsed_title` from the existing title, user-confirmed canonical correction preserves the prior parsed title as a user alias, and a later auto correction using that alias attaches to the same anime without renaming its canonical title.
- [ ] Add `006_parsed_title.sql` with `ALTER TABLE media_file ADD COLUMN parsed_title TEXT NOT NULL DEFAULT ''; UPDATE media_file SET parsed_title=title WHERE parsed_title='';`; add its `file(READ ...)` and migration dependency to `backend/CMakeLists.txt`; register migration 6 and update schema version bounds.
- [ ] Add `parsedTitle` to media DTO/JSON. During scanner persistence initialize it to the parser's extracted title; correction updates `title` but never `parsed_title`.
- [ ] In `updateMediaCorrection`, keep the existing transaction; on explicit user correction, add the stored parsed title and prior anime display title as normalized user aliases, update the anime display title to the confirmed correction title, and store the correction title on media. On `automaticScan`, resolve aliases and attach media, but do not mutate the anime canonical title or aliases.
- [ ] Rebuild `titleIndex_` correctly after alias/display-title changes. Run focused SQLite/repository and ManagementApi tests.

## Task 3: Canonical target naming

- [ ] Add a failing TargetPlanner expectation for a target filename `与奔驰于透明之夜的你，谈一场看不见的恋爱 [01].mkv`; assert SP/OVA/NCOP/NCED are emitted as bracketed markers such as `[SP01]`.
- [ ] Implement the bracketed filename stem and rerun TargetPlanner tests.
- [ ] Add a MediaService/OrganizationHttp test: a media record whose parsed title is an alias of `anime.displayTitle` previews under the canonical anime directory and canonical prefixed filename; an unlinked record continues using its corrected media title.
- [ ] In preview resolve `animeId` to its `AnimeRecord` and pass `displayTitle` to TargetPlanner; missing association falls back to `media.title`, while a dangling association yields the existing not-found/safe error path.
- [ ] Run OrganizationService, OrganizationHttp, SqliteRepository, ManagementApi, FilenameParser and TargetPlanner tests.

## Task 4: Inbox clarity and documentation

- [ ] Add an Inbox test that displays the immutable parsed filename title separately from the editable canonical title.
- [ ] Add a read-only parsed-title label near the correction editor; label the editable field `规范标题`. Do not make parsed title editable or send it in the correction request.
- [ ] Document in README that actual downloaded filenames are parsed; the user-confirmed title is canonical; parsed title is retained as an alias; organized episode names use `[NN]`; existing organized files are not moved automatically.
- [ ] Run full CMake configure/build, full CTest, frontend tests, lint and production build. Build into a separate ASCII output directory while the running backend's executable is in use. Smoke with disposable paths only; verify the source filename, parsed-title DTO, alias re-association, canonical preview and output, and source preservation.

## Final review

- [ ] Check every write path: user-confirmed correction may add aliases/update canonical title; automatic scan may only resolve aliases; Bangumi bind stays explicitly confirmed; preview/execute use a canonical target but preserve the original source.
- [ ] Confirm the migration works for schema version 5 databases and fresh databases; no test touches `D:\追番`.
- [ ] Commit the complete behavior after verification and report any compatibility limitation.
