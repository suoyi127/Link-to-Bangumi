# Folder Import Resource Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a third, safe folder-import source to the renamed 资源库 page without copying source videos.

**Architecture:** SQLite stores registered canonical folder roots and associates each imported media row with one root. The repository exposes `folder_import` as a logical third origin while preserving the existing SQLite origin constraint. Scan, preview, organization, and playback resolve and revalidate the registered root for each operation.

**Tech Stack:** C++20, SQLite, Drogon, React/TypeScript, Ant Design, Catch2, Vitest.

---

### Task 1: Persistent source identity

**Files:** `backend/migrations/007_folder_import.sql`, `backend/src/infrastructure/database/InitialSql.hpp.in`, `backend/CMakeLists.txt`, `backend/src/infrastructure/database/SqliteDatabase.cpp`, `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/include/anime_vault/infrastructure/database/SqliteMediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`, `backend/tests/integration/SqliteRepositoryTest.cpp`.

- [ ] Write a failing repository test: two folder roots are registered, media from each is returned with logical origin `folder_import`, and marking one folder missing leaves the other's row unchanged.
- [ ] Build and run `anime_vault_sqlite_repository_tests`; observe the new test fail before implementation.
- [ ] Add migration 007 with `folder_import(id,root_path,created_at)` and nullable `media_file.folder_import_id` FK. Add repository methods `addFolderImport`, `listFolderImports`, `getFolderImport`; extend `MediaRecord` with optional `folderImportId`. Store folder media using DB origin `external_import`, but return logical origin `folder_import`; filter inbox queries and missing reconciliation by folder ID.
- [ ] Rebuild and run repository tests; ensure older qB/import records remain unchanged.

### Task 2: Validated folder scan and API

**Files:** `backend/include/anime_vault/api/MediaService.hpp`, `backend/src/api/MediaService.cpp`, `backend/src/api/MediaController.cpp`, `backend/tests/integration/MediaServiceContractTest.cpp`, `backend/tests/integration/OrganizationHttpTest.cpp`.

- [ ] Write a temporary-directory test showing `addFolderImport(path)` rejects drive roots and overlap with qB/import/library/registered folders; `createFolderScan(id)` discovers a video and rescan marks only that folder's missing file.
- [ ] Run the contract target and observe the new test fail.
- [ ] Implement `GET /api/folder-imports`, `POST /api/folder-imports` with JSON `{path}`, and `POST /api/folder-imports/{id}/scan`; validate UTF-8 absolute paths and current canonical identity, return stable errors, use `scanFrom` with folder ID and no destructive operation.
- [ ] Build and run the contract and HTTP targets; confirm three origins paginate independently.

### Task 3: Organization and playback safety

**Files:** `backend/src/api/MediaService.cpp`, `backend/src/services/OrganizationService.cpp`, `backend/src/services/PlaybackService.cpp`, `backend/tests/integration/OrganizationServiceTest.cpp`, `backend/tests/unit/PlaybackServiceTest.cpp`.

- [ ] Add failing tests for folder-source preview/organization/playback and for a source outside its registered root.
- [ ] Run focused tests to confirm the missing behavior.
- [ ] Resolve `folder_import` media to its registered root in each service and revalidate containment and source snapshot before use; qB completion confirmation remains qB-only.
- [ ] Rebuild and run focused tests.

### Task 4: Three-tab resource library

**Files:** `frontend/src/App.tsx`, `frontend/src/api/types.ts`, `frontend/src/api/client.ts`, `frontend/src/components/InboxPage.tsx`, `frontend/src/components/InboxPage.test.tsx`, `frontend/src/api/client.test.ts`.

- [ ] Add a failing UI test requiring the “资源库” navigation label, a third “文件夹导入” tab, path submission, folder scanning, and its own file list.
- [ ] Run focused Vitest and confirm failure.
- [ ] Add typed API methods and the third tab. Keep pagination state per origin; show registered roots and scan actions only in the folder tab. Continue using existing correction/preview/confirm flow for all three sources.
- [ ] Run focused frontend tests, lint, and production build.

### Task 5: Integration verification and handoff

**Files:** `README.md`.

- [ ] Document that the user pastes an absolute local directory path; the app scans it in place and does not copy or move it.
- [ ] Build backend and run CTest; verify frontend tests/lint/build, then restart the local backend and check `/health` plus the resource-library page.
- [ ] Review `git diff --check` and report exact results and remaining limitations without touching real media under `D:/追番`.
