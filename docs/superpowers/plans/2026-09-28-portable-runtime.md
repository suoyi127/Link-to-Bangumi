# Portable Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Start on a clean Windows profile without qB or `D:/追番`, and let the user persist a separate qB download directory.

**Architecture:** The existing SQLite `setting` key/value table stores the chosen directory. A small framework-independent path resolver chooses per-user defaults and validates changes; the backend uses a private empty sentinel until a qB directory is configured. A dedicated API writes the path and reports that a restart is needed.

**Tech Stack:** C++20, Drogon, SQLite, Catch2, React/TypeScript, Vitest.

---

### Task 1: Persist and resolve the qB directory

**Files:** Modify `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`, `backend/src/main.cpp`; create `backend/include/anime_vault/services/RuntimePaths.hpp`, `backend/src/services/RuntimePaths.cpp`, `backend/tests/unit/RuntimePathsTest.cpp`; modify `backend/CMakeLists.txt`, `backend/tests/CMakeLists.txt`.

- [ ] Write Catch2 tests that call `resolveRuntimePaths(data, importOverride, libraryOverride, qbOverride, storedQb)` using disposable roots. Assert no qB path gives `qbConfigured == false` and a private sentinel, a stored path wins absent an override, and an explicit override wins. Add SQLite round-trip assertions for a persisted `qbDownloadDirectory` preference.
- [ ] Build only the new test target and run it; observe the expected missing-API failure before implementing.
- [ ] Add `qbDownloadDirectory` to `UiPreferences` and its existing SQLite key/value load/save. Resolve `%LOCALAPPDATA%/AnimeVault` and the user's Videos directory on Windows, with non-Windows fallbacks for tests. Never default to `D:/追番`. Create the private sentinel under the data root, but do not create a user download directory.
- [ ] Rebuild and run the focused tests. Commit the runtime-path unit.

### Task 2: Reject qB operations while unconfigured

**Files:** Modify `backend/include/anime_vault/api/MediaService.hpp`, `backend/src/api/MediaService.cpp`, `backend/src/main.cpp`; modify `backend/tests/integration/MediaServiceContractTest.cpp`.

- [ ] Add a disposable-directory test constructing `MediaService` with `qbConfigured=false`: `createScan()` must throw `ApiError` with `qb_download_dir_unconfigured`, while `createImportScan()` can still run. Run it and observe failure.
- [ ] Add the flag and guard before source validation. Construct the service with the resolved runtime source. Add the same guard to both Mikan feed/rule creation handlers before they call qB. Existing configured fixtures retain their behavior.
- [ ] Rebuild and run focused tests. Commit the operation gate.

### Task 3: Validate and save source selection

**Files:** Create `backend/include/anime_vault/services/RuntimePaths.hpp` and `backend/src/services/RuntimePaths.cpp` validation declarations/definitions; modify `backend/include/anime_vault/api/MediaController.hpp`, `backend/src/api/MediaController.cpp`, `backend/tests/integration/ManagementApiTest.cpp`, `backend/src/main.cpp`.

- [ ] Add disposable tests for absolute existing independent folder, relative path, missing folder, filesystem root, nested/overlapping import/library/data roots, and a saved-path round trip through `PUT /api/settings/qb-download-directory` followed by `GET /api/settings`. Run and observe failures.
- [ ] Implement `validateQbDownloadDirectory` using canonical paths and containment checks. Add an explicit request DTO (`path` string only, at most 4096 bytes), stable `invalid_qb_download_directory` / `overlapping_roots` errors, and a 200 response with `restartRequired=true`. An empty path clears the choice. Return both active `sourcePath` and saved `qbDownloadDirectory`; `qbDownloadConfigured` describes the active server. Preserve the existing preferences PUT by carrying forward the saved directory.
- [ ] Rebuild and run focused tests. Commit the settings API.

### Task 4: Update settings UI

**Files:** Modify `frontend/src/api/types.ts`, `frontend/src/api/client.ts`, `frontend/src/components/SettingsPage.tsx`, `frontend/src/components/SettingsPage.test.tsx`.

- [ ] Add tests for unconfigured state, saving a selected directory, restart notice, and disabled Mikan actions. Run Vitest and observe failures.
- [ ] Add the API client call and an editable qB directory input with a save button. Show the active and pending paths separately and explain that existing files are not moved. Disable feed/rule creation unless both qB Web UI and qB download directory are configured.
- [ ] Run focused frontend tests and commit the UI.

### Task 5: Milestone verification and documentation

**Files:** Modify `README.md` only as needed to explain first-run configuration and developer environment overrides.

- [ ] Run `cmake --preset test`, `cmake --build --preset test`, `ctest --preset test`, `npm test -- --run`, `npm run lint`, and `npm run build`; record exit codes and failures. Do not run tests on `D:/追番`.
- [ ] Start the backend with disposable data/import/library directories and no qB directory; confirm `GET /api/health` and `GET /api/settings` work and `POST /api/scans` returns `qb_download_dir_unconfigured`.
- [ ] Run `git diff --check`, inspect the exact changed files, and commit docs/verification fixes. Do not claim the installer or desktop host is complete; those are subsequent subprojects.
