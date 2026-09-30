# Dual-Source Title Enrichment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bind and Chinese-label unbound romanized media via Bangumi aliases when Mikan has no matching RSS pair.

**Architecture:** Keep Mikan enrichment unchanged. Add an alias-aware Bangumi lookup to the existing transport/service boundary; verify one discovered subject against its v0 `infobox` aliases before the enricher binds it. Canonicalize only untouched local titles.

**Tech Stack:** C++20, Drogon/WinHTTP, SQLite, Catch2, CMake.

---

### Task 1: Alias-aware search and verification

**Files:** `backend/include/anime_vault/ports/BangumiTransport.hpp`, `backend/include/anime_vault/infrastructure/network/DrogonBangumiTransport.hpp`, `backend/src/infrastructure/network/DrogonBangumiTransport.cpp`, `backend/include/anime_vault/infrastructure/network/BangumiConnectionManager.hpp`, `backend/src/infrastructure/network/BangumiConnectionManager.cpp`, `backend/include/anime_vault/services/BangumiMatcher.hpp`, `backend/src/services/BangumiMatcher.cpp`, `backend/include/anime_vault/services/BangumiService.hpp`, `backend/src/services/BangumiService.cpp`, `backend/tests/integration/BangumiServiceTest.cpp`.

- [x] Write a failing test where v0 search has no candidates, alias search returns one ID, and v0 subject details include a matching Romanized alias. Assert a verified subject result; assert ambiguous or unrelated aliases are rejected.
- [x] Build and run the focused test; confirm it fails for missing alias lookup behavior.
- [x] Add a legacy-search transport method using `GET /search/subject/<encoded keyword>?type=2&responseGroup=small`; parse bounded results and subject `infobox` aliases. Return only a verified unique animation subject, with one distinctive-prefix retry for long ASCII titles.
- [x] Run focused service tests; confirm the original search behavior remains green.

### Task 2: Safe automatic binding and Chinese display title

**Files:** `backend/include/anime_vault/repositories/MediaRepository.hpp`, `backend/include/anime_vault/infrastructure/database/SqliteMediaRepository.hpp`, `backend/src/infrastructure/database/SqliteMediaRepository.cpp`, `backend/src/services/AnimeEnricher.cpp`, `backend/tests/integration/BangumiServiceTest.cpp`, `backend/tests/integration/AnimeBindingTest.cpp`.

- [x] Write a failing integration test: a romanized, untouched entry with no Mikan mapping becomes bound and displays the verified Bangumi Chinese title; user-edited/locked entries remain unchanged.
- [x] Build and run the focused test; confirm it fails for missing fallback behavior.
- [x] Call alias lookup only if normal ranking cannot auto-bind. Update display title and alias in the same repository binding path only for untouched entries, without moving or renaming files.
- [x] Run focused binding/enrichment tests and a backend build.

### Task 3: Runtime smoke check

- [x] Restart the backend built from these changes, leaving the frontend running.
- [x] Check `/health` and one enrichment sync; report bound titles and any external service limitation without exposing credentials.
