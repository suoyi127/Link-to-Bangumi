# Manual Import Ingress Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox syntax. Keep only one focused test per boundary as the user requested.

**Goal:** Preserve `D:\追番\番剧` as qB-only while admitting stable files from a separate, explicitly triggered `D:\追番\外来导入` folder with durable provenance.

**Architecture:** The existing scanner/parser and inbox persistence are reused with a second long-lived scanner. The database marks each media row as `qb_download` or `external_import`. Import scanning has its own route and never copies, moves or deletes source files.

**Tech Stack:** C++20, Drogon, SQLite C API, Catch2, CMake.

---

## Context and file map

- Read `AGENTS.md`, `docs/superpowers/specs/2026-09-25-trusted-local-ingress-design.md`, current `MediaService`, `DirectoryScanner`, SQLite repository, and migrations 001–003.
- `backend/migrations/004_media_origin.sql`: add `media_file.origin TEXT NOT NULL DEFAULT 'qb_download'` with a check or equivalent validation; the default preserves prior rows.
- `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`: carry `origin` through insert/upsert/read; never silently change an existing row's origin.
- `backend/src/infrastructure/database/SqliteDatabase.cpp`, `InitialSql.hpp.in`, and `backend/CMakeLists.txt`: embed and apply migration 004 transactionally, schema version 4.
- `backend/include/anime_vault/api/MediaService.hpp`, `backend/src/api/MediaService.cpp`: optional second scanner and `createImportScan`; use one helper for parse/persist, with explicit origin.
- `backend/src/api/MediaController.cpp`, `backend/src/main.cpp`, `.env.example`, `README.md`: manual route, configured import root, documentation.
- Focused tests: `backend/tests/integration/SqliteRepositoryTest.cpp` and `backend/tests/integration/MediaServiceContractTest.cpp`. Tests use disposable source/import/library/data trees, never `D:\追番`.

## Task 1: Durable provenance

- [ ] First extend the existing temporary SQLite smoke test: migrate a v0 DB; insert one `qb_download` and one `external_import` media row; reopen and assert both origins are returned unchanged. Reinsert the import row with a contradictory origin and assert its stored origin remains `external_import`.
- [ ] Run the focused test and observe RED because `MediaRecord` and schema have no origin field. If CMake is unavailable, use a standalone MSVC/SQLite build for this one smoke test and record the command.
- [ ] Add `std::string origin{"qb_download"}` to `MediaRecord`. Add ordered migration 004 and update the embedded SQL/schema version. `insertMedia` binds origin on first insert; its `ON CONFLICT(source_path) DO UPDATE` must not update origin. `getMedia` and `listInbox` return origin. Reject values other than `qb_download` and `external_import` before SQLite writes.
- [ ] Run the focused test again; expected GREEN. Commit `feat: persist media source provenance`.

## Task 2: Explicit import scan

- [ ] First extend the existing disposable service contract test: create one video under the qB source and one under import; call qB scan twice (the scanner uses snapshots), assert only the qB file is in the inbox. Then call import scan twice and assert the import file appears as `external_import` while qB remains `qb_download`. Assert neither source file changed. Use zero stability window only in this test; production keeps the configured stability window.
- [ ] Run the focused test and observe RED because the import scanner/method is absent.
- [ ] Add `ANIME_VAULT_IMPORT_DIR` with local default `D:/追番/外来导入`. Keep the qB scanner long-lived and create the import scanner lazily on the first explicit import call, retaining it across later calls for stability snapshots. Canonicalize and reject identical, nested, or overlapping source/import/library roots; a missing import root returns stable `import_root_unavailable` only for the import action, not a startup failure or automatic directory creation. Refactor the existing scan parse/persist loop into one helper parameterized by scanner and origin. Keep the current scan rate limit, DB mutex, and source-stability behavior.
- [ ] Register `POST /api/imports/scan` as the only import trigger. It accepts no path/body, returns the same scan DTO shape, and never accepts arbitrary user paths. Add `origin` to inbox JSON. Validate malformed bodies and map root/configuration errors to stable 400/409 application codes with request ID.
- [ ] Update `.env.example` and `README.md` to explain qB-only source, initially empty manual import folder, no automatic import, and provenance limitations while qB Web API is disabled.
- [ ] Run the focused test and backend syntax/build check where available; expected GREEN. Commit `feat: scan explicit external import folder`.

## Gate

- [ ] Confirm `D:\追番\番剧` and `D:\追番\外来导入` were not modified by tests or implementation commands. Confirm the route cannot select arbitrary paths.
- [ ] Run CMake configure/build/CTest and frontend test/lint/build; report exact blocked prerequisites rather than treating missing tests as a pass. Keep test scope minimal.
- [ ] Do not connect the organization-execution route in this plan. The accepted trusted-local-directory assumption and explicit qB completion confirmation belong to the later execution plan.
