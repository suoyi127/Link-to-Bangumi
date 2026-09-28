# qB Web UI Page Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Configure, test, and activate a local qBittorrent Web UI connection entirely from Anime Vault's Settings page.

**Architecture:** A Windows Credential Manager record scoped to the data directory stores the complete connection. A mutex-protected manager returns immutable qB clients to existing status/RSS operations and replaces the active client on save. A dedicated local-only JSON API handles configuration; the frontend never receives the password.

**Tech Stack:** C++20, Drogon, WinCred/Advapi32, Catch2, React/TypeScript, Vitest.

---

### Task 1: Validate local qB endpoint and generalize the client

**Files:** `backend/include/anime_vault/infrastructure/network/QbWebClient.hpp`, `backend/src/infrastructure/network/QbWebClient.cpp`, `backend/tests/unit/QbWebClientTest.cpp`.

- [ ] Add failing tests that `http://[::1]:8080` and `http://127.0.0.1:8090` parse to explicit address, port, Host header and Referer; reject `localhost`, remote IP, embedded userinfo, non-HTTP scheme, path/query/fragment, control characters and out-of-range ports. Test the qB landing-page signature gate.
- [ ] Run `cmake --build --preset test --target anime_vault_qb_web_client_tests` and `ctest --test-dir build/test -C Debug -R '^QbWebClient$' --output-on-failure`; confirm the new assertions fail.
- [ ] Implement `parseLocalQbEndpoint(std::string_view)` with a strict explicit-host URL grammar, and let `QbWebClient` construct Drogon client and headers from it. Require the verified qB landing-page marker before the login request for each operation.
- [ ] Re-run the focused test to green; commit the client change.

### Task 2: Protect and hot-swap the connection

**Files:** `backend/include/anime_vault/infrastructure/network/QbConnectionManager.hpp`, `backend/src/infrastructure/network/QbConnectionManager.cpp`, `backend/tests/unit/QbConnectionManagerTest.cpp`, `backend/CMakeLists.txt`, `backend/tests/CMakeLists.txt`.

- [ ] Write failing Catch2 tests for an isolated data-directory Credential Manager target: saved URL/user/password round trip, no password in public summary, blank-password edit retention, clear, and replacement of the current client; verify environment fallback when there is no saved record. Clean the test-only credential target at test end.
- [ ] Run the new target and confirm expected failures.
- [ ] Implement `QbConnectionConfig {url,username,password}`, `QbConnectionManager::current()/summary()/save()/clear()/draft()` and WinCred-backed storage. Keep the record under `AnimeVault/qb-webui/<absolute-data-path>`, use `CRED_TYPE_GENERIC` and `CRED_PERSIST_LOCAL_MACHINE`, reject invalid/decryption failures rather than silently falling back, and link `Advapi32` only on Windows. On non-Windows, report `qb_config_storage_unavailable` for saved-record writes.
- [ ] Re-run focused tests and commit.

### Task 3: Add password-free configuration API and switch qB consumers

**Files:** `backend/include/anime_vault/api/QbConfigController.hpp`, `backend/src/api/QbConfigController.cpp`, `backend/src/main.cpp`, `backend/src/api/MediaController.cpp`, `backend/include/anime_vault/api/MediaController.hpp`, `backend/tests/integration/QbConfigHttpTest.cpp`, `backend/CMakeLists.txt`, `backend/tests/CMakeLists.txt`.

- [ ] Write failing HTTP contract tests for GET (never returns password), PUT validation and immediate state change, blank-password edit, DELETE fallback, POST test without persistence, malformed JSON, oversized bodies, and stable error codes.
- [ ] Run the focused integration test and confirm failure.
- [ ] Register the four configuration methods under `/api/qb/config` and `/api/qb/config/test` with explicit request/response DTOs. In `main.cpp`, create the manager from the data path and environment fallback, then take a client snapshot inside every status/RSS request. Make `/api/settings` compute `qbWebUiConfigured` from the manager instead of a startup bool. Preserve the legacy `qbWebUiUrl` preference field only for wire compatibility.
- [ ] Run focused HTTP tests and commit.

### Task 4: Build the Settings-page connection form

**Files:** `frontend/src/api/types.ts`, `frontend/src/api/client.ts`, `frontend/src/components/SettingsPage.tsx`, `frontend/src/components/SettingsPage.test.tsx`.

- [ ] Add failing tests for URL/user/password form, hidden/empty password on load, Save with blank password retaining an existing saved secret, Test connection displaying a concrete status, Clear saved config, and no password leakage into ordinary preferences.
- [ ] Run `npm.cmd --prefix frontend test -- --run src/components/SettingsPage.test.tsx` and confirm the new cases fail.
- [ ] Add typed API calls and a dedicated qB connection section. Reload the public summary, `/api/qb/status`, and RSS status after save/clear; remove the misleading legacy URL edit control. Keep password only in component state and clear it after actions.
- [ ] Run focused frontend tests and commit.

### Task 5: Milestone verification and delivery

- [ ] Run `cmake --preset test` with `VCPKG_ROOT=D:\av-build-cache\vcpkg`, `cmake --build --preset test`, and `ctest --preset test`.
- [ ] Run `npm.cmd --prefix frontend test -- --run`, `npm.cmd --prefix frontend run lint`, and `npm.cmd --prefix frontend run build`.
- [ ] Run `git diff --check`; verify the working tree contains only intended changes. Check local qB connectivity using the existing loopback service without modifying torrents.
- [ ] Commit any final corrections and report exact test results, connection status, and how to use the Settings page. Do not automatically push or alter the user's qB torrents.
