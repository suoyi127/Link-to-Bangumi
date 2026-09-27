# Local mpv Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Launch a selected local media file in mpv from its anime detail page.

**Architecture:** A playback service resolves a repository media ID to a contained regular file, then calls a process-launcher interface. A small platform wrapper launches the executable without a shell. A POST endpoint validates the request and the React detail page calls it.

**Tech Stack:** C++20, Drogon, SQLite, Catch2, React, TypeScript, Vitest.

---

### Task 1: Playback domain and process wrapper

**Files:** `backend/include/anime_vault/services/PlaybackService.hpp`, `backend/src/services/PlaybackService.cpp`, `backend/include/anime_vault/infrastructure/ProcessLauncher.hpp`, `backend/src/infrastructure/ProcessLauncher.cpp`, `backend/tests/unit/PlaybackServiceTest.cpp`, `backend/CMakeLists.txt`, `backend/tests/CMakeLists.txt`.

- [ ] Write Catch2 tests with temporary source/library/import roots and a recording launcher: valid library file yields one launch with executable plus `--save-position-on-quit`, `--`, and file; missing media and outside/symlink paths yield stable errors without launching.
- [ ] Run the new test target and observe failure before implementing.
- [ ] Implement file selection, canonical containment, and a detached OS process wrapper using `CreateProcessW` or `posix_spawn`; never invoke a shell.
- [ ] Run the focused test to green.

### Task 2: Repository and HTTP API

**Files:** `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`, `backend/include/anime_vault/api/MediaController.hpp`, `backend/src/api/MediaController.cpp`, `backend/src/main.cpp`, `backend/tests/integration/OrganizationHttpTest.cpp`.

- [ ] Add a failing test for persisted `library_path` mapping and playback request ID parsing/rejection.
- [ ] Run the focused tests and observe failure.
- [ ] Expose the existing `library_path` on `MediaRecord`, register `POST /api/media/{id}/play`, validate empty input and local Origin, and map service errors to stable HTTP codes.
- [ ] Run the focused tests to green.

### Task 3: Frontend

**Files:** `frontend/src/api/client.ts`, `frontend/src/components/AnimeDetailPage.tsx`, `frontend/src/components/AnimeDetailPage.test.tsx`, `frontend/src/components/SettingsPage.tsx`.

- [ ] Add a failing component test: clicking an item's play button calls the ID-only API and handles an error.
- [ ] Run that test and observe failure.
- [ ] Add API call, per-item busy/error UI, and update the settings label.
- [ ] Run focused frontend tests to green.

### Task 4: Delivery verification

- [ ] Run CMake configure/build and CTest, frontend tests/lint/build per repository rules.
- [ ] Check a loopback API error response without launching real mpv; report exact results and frontend address.
