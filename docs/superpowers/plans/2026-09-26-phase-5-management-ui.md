# Phase 5 Management UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the approved Windows-local phase 0–5 dashboard, inbox, library/detail and settings workflows on top of the verified backend.

**Architecture:** Keep the existing React/Ant Design shell and state navigation. A typed same-origin API client is the only browser/backend boundary. Add bounded backend read/settings endpoints and expose media anime IDs; UI pages each own their loading, empty, error and confirmation states. No player, qB Web UI enablement, source deletion or live-network tests.

**Tech Stack:** C++20/Drogon/SQLite/Catch2; React 19, TypeScript, Ant Design 5, Vitest/Testing Library, Vite.

---

## Preconditions and file map

Read `AGENTS.md`, `docs/superpowers/specs/2026-09-26-phase-5-management-ui-design.md`, approved phase 0–5 design, current `MediaController`, `MediaRepository`, `SqliteMediaRepository`, `App.tsx`, `vite.config.ts`, and current tests. Work in `D:\代码库\anime-vault` feature workspace. Phase 4 gate: CMake configure/build and CTest 14/14 passed; frontend test/lint/build passed with a non-blocking >500kB chunk warning. Tests may use disposable paths only, never `D:\追番`. The qB source scan requires the user to attest completion before organizing.

Files by responsibility:

- Backend `MediaRepository.hpp`, `SqliteMediaRepository.hpp/.cpp`: typed scan/audit/settings records and bounded reads/writes. No migration is needed; `setting` and `audit_log` exist.
- Backend `MediaController.hpp/.cpp`, `main.cpp`: route DTOs and effective runtime configuration, including safe read-only paths and Bangumi configured state.
- Frontend `src/api/client.ts`, `src/api/types.ts`: stable error/request ID handling and exact DTOs. `vite.config.ts` proxies `/api` and `/health` to `127.0.0.1:8848`.
- Frontend `src/pages/DashboardPage.tsx`, `InboxPage.tsx`, `LibraryPage.tsx`, `AnimeDetailPage.tsx`, `SettingsPage.tsx`: focused page components. `App.tsx` only owns navigation and common shell.
- Frontend tests beside the client/pages. Prefer focused mocked-fetch component tests; no live server in unit tests.

## Task 1: Backend read/settings contracts

**Files:** Modify `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/include/anime_vault/infrastructure/database/SqliteMediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`, `backend/include/anime_vault/api/MediaController.hpp`, `backend/src/api/MediaController.cpp`, `backend/src/main.cpp`; create `backend/tests/integration/ManagementApiTest.cpp`; register focused target in `backend/tests/CMakeLists.txt`.

- [ ] Write a failing disposable-SQLite/route test: `GET /api/scans?limit=2&offset=0` returns newest-first items and `nextOffset`; `GET /api/audit-logs?limit=2&offset=0` returns sanitized, paginated records; a media response includes its nullable positive `animeId`; `GET /api/settings` shows effective paths and `bangumiConfigured`; `PUT /api/settings` accepts only bounded preferences and leaves runtime paths unchanged. Assert invalid limit/offset, unknown field and oversized body return stable 400 codes. Run `cmake --build build/ascii-gate --config Debug --target anime_vault_management_api_tests`, then `ctest --test-dir build/ascii-gate -C Debug -R ManagementApi --output-on-failure`; RED must be missing route/repository contract, not dependencies.
- [ ] Define explicit records in `MediaRepository.hpp`, for example `struct AuditRecord { std::int64_t id; std::string action, entityType, entityId, createdAt; }; struct UiPreferences { std::string preferredOperation{"hardlink"}, mpvExecutable, qbWebUiUrl; int scanIntervalSeconds{3600}; };` Add bounded list methods (`limit` 1–100, `offset` 0–1,000,000) and `getUiPreferences`/`putUiPreferences`. SQL uses prepared statements, connection mutex, `LIMIT limit+1`, allowlisted `setting` keys, and no audit `details_json` in public response. Record a sanitized `anime_bound` audit entry inside the existing binding transaction so detail has a local history. Add `animeId` to `MediaRecord`/media query aliases and JSON, preserving null when absent.
- [ ] Register `GET /api/scans`, `GET /api/audit-logs` (optional positive `animeId` filter for binding history), `GET /api/settings`, `PUT /api/settings`; parse numeric query with `from_chars`, allowlist fields, cap PUT body at 4KiB, and reuse the current stable `ApiError`/request-ID response path. `GET settings` reports canonical effective source/import/library/data strings and booleans for Bangumi configured and qB Web UI not configured; it must not leak User-Agent contents or secrets. `PUT` persists only `preferredOperation` (`hardlink|copy|symlink`), `scanIntervalSeconds` (60–86400), `mpvExecutable` (max 1024 UTF-8 bytes), `qbWebUiUrl` (max 2048 bytes, optional loopback HTTP(S) URL without credentials/query; reserved, not used for requests). Exclude path mutation and proxy-bridge changes.
- [ ] Rebuild and run the focused test to GREEN, then affected CTest (`SqliteRepository|OrganizationHttp|ManagementApi`). Commit `feat: expose bounded management reads and preferences`.

## Task 2: Typed browser client and dashboard

**Files:** Create `frontend/src/api/client.ts`, `frontend/src/api/types.ts`, `frontend/src/api/client.test.ts`, `frontend/src/pages/DashboardPage.tsx`, `frontend/src/pages/DashboardPage.test.tsx`; modify `frontend/vite.config.ts`, `frontend/src/App.tsx`.

- [ ] Write a failing mocked-fetch test: JSON success returns typed data; non-2xx yields `ApiError` with backend `error.code` and `requestId`; abort/network errors become a safe offline state; dashboard renders counts/recent scan, invokes `POST /api/scans` or `/api/imports/scan` only on separate button clicks, and refreshes after success. Run `npm.cmd test -- --run --configLoader runner` in `frontend`; RED should identify absent client/page.
- [ ] Implement `requestJson<T>(path, init?)` with same-origin paths only, `Accept: application/json`, safe JSON parsing, 20-second abort timeout for scan actions, and `ApiError { code, requestId, status }`. Keep exact DTOs for `ScanRecord`, `MediaRecord` (including `animeId`), `AnimeRecord`, `BangumiCandidate`, `PreviewResponse`, `ExecutionResult`, `UiPreferences`; model nullable fields instead of guessing from titles. Set Vite `/api` proxy to the same loopback target as `/health`.
- [ ] Replace only the dashboard placeholder: show health, pending inbox count, library title count, newest scans and failure state; use `nextOffset` rather than assuming unbounded results; loading buttons prevent duplicate scans and show stable error code/request ID. Keep the shell's other placeholders until their tasks. Run the focused tests, `npm.cmd run lint`, `npm.cmd run build -- --configLoader runner --outDir dist-gate`; commit `feat: add typed client and scan dashboard`.

## Task 3: Inbox correction, search, preview and execution

**Files:** Create `frontend/src/pages/InboxPage.tsx`, `frontend/src/pages/InboxPage.test.tsx`; modify `frontend/src/App.tsx`, `frontend/src/api/client.ts`, `frontend/src/api/types.ts`, `frontend/src/components/DashboardPage.tsx`, `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/include/anime_vault/infrastructure/database/SqliteMediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`, `backend/src/api/MediaController.cpp`, and a focused disposable SQLite/route test. The backend addition is necessary for real server-side inbox paging; Ant Design client-side table paging alone is not bounded.

- [ ] Write a failing mocked-flow test for inbox load, editable `title/season/episodeNumber/episodeType`, correction POST, local `animeId` navigation, Bangumi search with `localMatch` disclosure, operation preview, conflicts, and execution. Test that qB-origin execute is disabled until separate completion attestation plus explicit confirmation, that external-import flow does not require qB attestation, and that retrying one plan reuses its idempotency key. No test sends a real filesystem/network request. Run focused Vitest to RED.
- [ ] Implement `GET /api/inbox?limit=&offset=` with repository `LIMIT limit+1`, `nextOffset` and a bounded count query `total`; validate `limit` 1–100 and `offset` 0–1,000,000. Keep the default route compatible with existing consumers. Dashboard reads `total`, while inbox follows `nextOffset` rather than downloading every row. Correction submits only the four accepted fields to `POST /api/inbox/{id}/parse` (never `bangumiSubjectId`). Episode type options are exactly backend accepted `normal|sp|ova|ncop|nced`; unknown items require choosing a valid type before saving. Search calls `GET /api/bangumi/search?q=` with a 1–100 UTF-8 byte query and displays `localMatch`, ranked candidates, offline/rate errors. A candidate binding uses only the confirmed `PUT /api/anime/{animeId}/bangumi` route in detail; never silently bind from score.
- [ ] Preview calls `POST /api/organize/preview` with `mediaFileId` and selected `hardlink|copy|symlink`, then shows target path, expiry and conflicts. Saving/changing correction or item/operation invalidates the preview and closes confirmation. A conflicted or expired preview cannot open execution. Execution calls `POST /api/organize/execute` with stored `planId`, one generated printable ASCII idempotency key per plan, `confirmed: true`, and `qbDownloadComplete` from the separate checkbox for qB origin. Generate a new key only for a new plan, not a retry. Display stable errors and refresh the inbox after success. Run focused backend/frontend tests/lint/build; commit `feat: complete safe inbox workflow`.

## Task 4: Paginated library, detail and explicit Bangumi binding

**Files:** Create `frontend/src/pages/LibraryPage.tsx`, `frontend/src/pages/AnimeDetailPage.tsx`, `frontend/src/pages/LibraryPage.test.tsx`, `frontend/src/pages/AnimeDetailPage.test.tsx`; modify `frontend/src/App.tsx`, `frontend/src/api/types.ts` as needed.

- [ ] Write failing mocked-fetch tests: grid/list toggle persists in component state, `nextOffset` loads the 201st title, detail `nextMediaOffset` loads beyond 500, missing cover uses a local placeholder, offline Bangumi error leaves existing local aliases/media visible, and bind requires an explicit confirmation. Run focused Vitest to RED.
- [ ] Implement library list from `GET /api/anime?limit=&offset=`, never assume the first page is all data. Detail reads `GET /api/anime/{id}?mediaLimit=&mediaOffset=` and shows aliases, media status/origin/path, Bangumi link, and available audit items. Search presents candidate title/year/episode score plus `localMatch`/season. Binding sends only `{subjectId, confirmed:true}` to `PUT /api/anime/{id}/bangumi` after the modal; refresh local detail after success. No playback/watch/cleanup actions or qB UI link.
- [ ] Run focused tests/lint/build and commit `feat: browse and bind local anime`.

## Task 5: Settings, audit and deliverable smoke

**Files:** Create `frontend/src/pages/SettingsPage.tsx`, `frontend/src/pages/SettingsPage.test.tsx`; modify `frontend/src/App.tsx`, `frontend/src/api/types.ts`, `README.md` (or current run guide); add one scripted/manual disposable-path smoke procedure in docs.

- [ ] Write a failing mocked-fetch test: effective paths are read-only, Bangumi and qB show honest configured/unconfigured states, preferences save through allowlisted PUT, inactive mpv/qB/scan-interval reservations are labeled, and audit pagination uses `nextOffset`. Run focused Vitest to RED.
- [ ] Implement settings/audit, split large Ant Design page chunks by lazy page imports if the Vite >500kB warning remains, and document Windows launch with `ANIME_VAULT_SOURCE_DIR`, `ANIME_VAULT_IMPORT_DIR`, `ANIME_VAULT_LIBRARY_DIR`, `ANIME_VAULT_DATA_DIR`, optional identifiable `ANIME_VAULT_BANGUMI_USER_AGENT`, backend loopback port, and frontend Vite proxy. State clearly that qB Web UI is not enabled and qB completion remains manual attestation.
- [ ] Run CMake configure/build, full CTest, frontend Vitest/lint/production build. Launch backend and frontend only against temporary source/import/library/data directories, check `/health`, scan a disposable video fixture, correct it, preview/confirm hardlink, and view its anime detail. Never use live Bangumi or `D:\追番` in verification. Remove only exact generated temporary artifacts after path validation. Commit `feat: deliver phase five settings and local workflow`.

## Final review

Read all changed code against the approved phase 0–5 design and the phase-5 spec. Confirm no UI claims playback, cleanup, qB completion detection, qB Web UI enablement or remote dependency for local browsing. Report exact build/test/smoke outcomes and any remaining known limitation; do not call the release deliverable if a required flow or gate remains unverified.
