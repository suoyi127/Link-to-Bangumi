# Desktop Host and Developer Entry Point Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide `dev.ps1` for a Vite/Debug-backend workflow and a one-window `AnimeVault.exe` that owns a bundled backend and serves the same production frontend.

**Architecture:** Drogon serves production static assets only when given an explicit validated web root. A small self-contained WinForms/WebView2 host selects a loopback port, starts the C++ backend with an instance nonce, waits for a matching health response, and owns its lifetime. A packaging script assembles the host, backend DLLs, and Vite build into a directory; the installer follows in a separate milestone.

**Tech Stack:** C++20/Drogon, Catch2, React/Vite, PowerShell, .NET 10 WinForms, Microsoft.Web.WebView2 `1.0.4191.47`.

---

### Task 1: Production static files and instance identity

**Files:** Modify `backend/include/anime_vault/api/HealthController.hpp`, `backend/src/api/HealthController.cpp`, `backend/src/main.cpp`, `backend/tests/integration/HealthEndpointTest.cpp`; create `backend/include/anime_vault/services/RuntimeWebRoot.hpp`, `backend/src/services/RuntimeWebRoot.cpp`, `backend/tests/unit/RuntimeWebRootTest.cpp`; modify `backend/CMakeLists.txt`, `backend/tests/CMakeLists.txt`.

- [ ] Extend the health test first: no nonce keeps the existing exact two-field JSON; a 32-character lowercase hex nonce adds `instanceToken` without JSON injection. Build/run to observe failure.
- [ ] Implement a validated optional nonce from `ANIME_VAULT_INSTANCE_TOKEN`, captured by the health route. Run the focused test green.
- [ ] Add a disposable-directory unit test for `validatedWebRoot`: it accepts an absolute folder with `index.html`, rejects relative/missing folders and an index symlink escaping the root. Build/run to observe failure.
- [ ] Configure Drogon's document root only after `validatedWebRoot`; use UTF-8 path conversion and retain API route precedence. Test the current 404 at `/` before the change and, after it, request `/`, `/assets/test.js`, `/health`, and `/api/settings` against a disposable web folder; require traversal to fail. Do not use actual media directories. Commit.

### Task 2: Minimal desktop process host

**Files:** Create `desktop/AnimeVault.Desktop/AnimeVault.Desktop.csproj`, `DesktopLayout.cs`, `BackendSession.cs`, `MainForm.cs`, `Program.cs`; create `desktop/AnimeVault.Desktop.Tests/AnimeVault.Desktop.Tests.csproj`, `Program.cs`.

- [ ] Write a dependency-free .NET test executable that creates a disposable installation layout and calls `DesktopLayout.Resolve`: it accepts `backend/anime_vault_server.exe` plus `web/index.html`, rejects missing files, and places WebView2 profile data under LocalAppData. Run `dotnet run --project desktop/AnimeVault.Desktop.Tests` to observe a compile failure before production classes exist.
- [ ] Implement `DesktopLayout` and rerun the test. Add `BackendSession` with loopback port reservation, random 128-bit instance nonce, hidden child process, matching `/health` poll, retry on port collision, and process-tree cleanup. Keep qB credentials and directory out of the package. Set the Bangumi User-Agent to `suoyi127/Link-to-Bangumi/0.1 (Windows)` only when absent.
- [ ] Add a WinForms form with a full-window WebView2 control, per-user profile folder, same-origin navigation guard, external-link handoff, and actionable startup/WebView2 errors. Pin stable WebView2 NuGet `1.0.4191.47`. Run `dotnet build desktop/AnimeVault.Desktop/AnimeVault.Desktop.csproj` and the layout self-test.

### Task 3: Developer and bundle scripts

**Files:** Create `dev.ps1`, `package-desktop.ps1`; modify `.gitignore`, `README.md`.

- [ ] Test preexisting missing commands (`Test-Path dev.ps1`, `Test-Path package-desktop.ps1` both false). Add `dev.ps1` with optional `-VcpkgRoot`, prompt fallback, Debug CMake configure/build, conditional `npm ci`, hidden direct backend/node child processes, printed local URLs, and cleanup on Ctrl+C. Reject occupied 8848/5173 without touching the occupying processes.
- [ ] Add `package-desktop.ps1`: require successful Release CMake build, frontend `dist`, and .NET publish; copy only required backend executable/DLLs, web assets, and published desktop files into `build/package/AnimeVault`. Do not copy databases, downloads, credentials, or user media.
- [ ] Document both paths and the bundled layout in `README.md`. Test `dev.ps1` with disposable environment paths and a nonconflicting port; test packaged backend/static files without requiring qB.

### Task 4: Milestone verification

**Files:** only corrections revealed by verification.

- [ ] Run `cmake --preset test`, `cmake --build --preset test`, `ctest --preset test`, `npm.cmd --prefix frontend test -- --run`, `npm.cmd --prefix frontend run lint`, `npm.cmd --prefix frontend run build`, .NET host build/self-test, and `git diff --check`.
- [ ] Assemble a package directory and launch `AnimeVault.exe` on this Windows machine. Confirm it spawns one backend, serves `/`, and terminates that child when closed. Record inability to verify a clean-machine installer until the next subproject.
- [ ] Review only changed files and commit. Leave existing dev backend processes and all `D:/追番` files untouched.
