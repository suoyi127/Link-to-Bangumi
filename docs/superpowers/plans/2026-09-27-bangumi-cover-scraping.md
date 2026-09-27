# Bangumi Cover Scraping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cache Bangumi covers locally for bound anime and allow a manual refresh without changing media files.

**Architecture:** A cover service validates Bangumi image URLs and bytes, downloads via a fixed-host adapter, writes to the app data directory, then conditionally updates the bound anime record. An API serves cached bytes and triggers refresh; the detail page exposes the action.

**Tech Stack:** C++20, Drogon, SQLite, Catch2, React, Vitest.

---

### Task 1: Safe cover data and persistence

**Files:** `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/include/anime_vault/infrastructure/database/SqliteMediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`, `backend/tests/integration/AnimeBindingTest.cpp`.

- [ ] Write a failing test: update a cover only when the current anime still has the expected Bangumi subject ID; reject a stale rebind.
- [ ] Run the focused test and confirm the missing method is the failure.
- [ ] Implement a conditional `UPDATE anime ... WHERE id=? AND bangumi_subject_id=?` plus an audit entry.
- [ ] Rerun the focused test.

### Task 2: Image fetch and cache service

**Files:** `backend/include/anime_vault/services/CoverScraper.hpp`, `backend/src/services/CoverScraper.cpp`, `backend/include/anime_vault/infrastructure/network/DrogonCoverTransport.hpp`, `backend/src/infrastructure/network/DrogonCoverTransport.cpp`, `backend/tests/integration/CoverScraperTest.cpp`, `backend/CMakeLists.txt`, `backend/tests/CMakeLists.txt`.

- [ ] Write failing tests for a `lain.bgm.tv` HTTPS URL, rejected foreign/HTTP URLs, valid JPEG/PNG/WebP signatures, oversized/invalid bodies, atomic cache publication and rebind race.
- [ ] Run the focused tests; confirm failures are due to the missing behavior.
- [ ] Implement a fixed-host Drogon transport and a service that fetches subject metadata through `BangumiService::subject`, validates bytes, writes a temporary file in the cache directory, atomically renames it, then conditionally updates the repository. Keep the old URL on failure.
- [ ] Rerun the focused tests.

### Task 3: API and UI

**Files:** `backend/src/api/MediaController.cpp`, `backend/include/anime_vault/api/MediaController.hpp`, `backend/src/main.cpp`, `frontend/src/api/client.ts`, `frontend/src/components/AnimeDetailPage.tsx`, `frontend/src/components/AnimeDetailPage.test.tsx`.

- [ ] Write a failing UI test for refresh and result feedback.
- [ ] Add `POST /api/anime/{id}/cover/refresh` with no body, stable error codes and an explicit response DTO. Add `GET /api/covers/{animeId}/{subjectId}` that serves only validated cached bytes for the current binding.
- [ ] After successful manual/automatic binding, start the cover service asynchronously; binding remains successful if the image fails.
- [ ] Add a detail-page refresh button and display the cached cover. Rerun the focused UI test.

### Task 4: Verification

- [ ] Configure and build CMake, run CTest, run frontend tests, lint and build.
- [ ] On a running local server, use only read-only checks or a disposable data directory. Do not change qB downloads.
- [ ] Update README with the required Bangumi User-Agent and cover-refresh behavior.
