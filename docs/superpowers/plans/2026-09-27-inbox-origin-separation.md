# Inbox Origin Separation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show qB downloads and external imports on separate, independently paginated inbox tabs.

**Architecture:** Add an optional, validated `origin` filter to the existing inbox API and apply it in SQLite before counting and paging. Keep the unfiltered API behavior for existing clients. The React page owns a page offset per origin and invalidates its active editing plan when switching tabs.

**Tech Stack:** C++20, Drogon, SQLite, React, Ant Design, Vitest.

---

### Task 1: Source-filtered inbox API

**Files:** `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/include/anime_vault/infrastructure/database/SqliteMediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`, `backend/include/anime_vault/api/MediaService.hpp`, `backend/src/api/MediaService.cpp`, `backend/src/api/MediaController.cpp`, `backend/tests/integration/MediaServiceContractTest.cpp`, `backend/tests/integration/OrganizationHttpTest.cpp`.

- [ ] Add an integration assertion using the existing two scanned files: `service.listInboxPage(0, 100, "qb_download")` has one qB item and total 1; the same call with `"external_import"` has one import item and total 1. Assert the unfiltered call still totals 2. Run the `MediaServiceContract` test and observe a compilation failure because the overload is absent.
- [ ] Add `std::optional<std::string> origin = std::nullopt` to `listInboxPage` declarations in repository, concrete repository, and service. In SQLite, reject any origin except `qb_download` or `external_import`; select `COUNT(*)` and rows using `WHERE status='inbox' AND origin=?` when filtered, and the existing queries otherwise. Bind origin before limit and offset in the filtered rows query.
- [ ] In `/api/inbox`, allow only `limit`, `offset`, `origin`; parse `origin` when supplied, return HTTP 400 code `invalid_origin` unless it is one of the two values, and pass it to `service.listInboxPage(offset, limit, origin)`. Keep the existing JSON response shape.
- [ ] In the existing `OrganizationHttpTest.cpp` fixture, request `/api/inbox?origin=external_import` and assert total 1, then request `/api/inbox?origin=invalid` and assert HTTP 400 with `error.code == "invalid_origin"`. Build and run `MediaServiceContract` and `OrganizationHttp`; expected result is both passing.

### Task 2: Two independent inbox tabs

**Files:** `frontend/src/api/client.ts`, `frontend/src/api/client.test.ts`, `frontend/src/components/InboxPage.tsx`, `frontend/src/components/InboxPage.test.tsx`.

- [ ] Add a failing client test asserting `getInbox(100, 0, undefined, 'qb_download')` fetches `/api/inbox?limit=100&offset=0&origin=qb_download`. Run only `client.test.ts` and confirm that URL assertion fails.
- [ ] Add a failing component test with one qB row and one import row: qB is the initial tab; switching to “外来导入” fetches import rows at its own offset; switching back preserves qB offset and clears selected editing/preview state. Run only `InboxPage.test.tsx` and confirm the tab is missing.
- [ ] Extend `getInbox` with an optional fourth origin argument and append its encoded query parameter only when present. Use an Ant Design `Tabs` control with keys `qb_download` and `external_import`, plus one offset per key. On tab change, invalidate selection and plan and fetch the selected origin. Keep one table and its editing flow, with `getInbox(100, currentOffset, signal, activeOrigin)` as its data source.
- [ ] Run the client and inbox component tests; confirm both pass and existing organize/confirm tests stay green.

### Task 3: Verification

**Files:** no new files.

- [ ] Run CMake configure/build and CTest, then frontend tests, lint and production build per `AGENTS.md`. Report any environmental or flaky-test limitation accurately.
- [ ] Run `git diff --check` and inspect the diff for unrelated edits. Do not push or alter actual qB/import files; this task only changes API queries and presentation.
