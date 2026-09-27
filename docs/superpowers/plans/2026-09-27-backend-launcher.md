# Windows Backend Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Launch an already-built Windows backend from a root-level batch file and explain its use in `README.md`.

**Architecture:** The batch file resolves candidate executables relative to itself and runs one in the foreground. It inherits the caller's environment and never performs a build or stores credentials.

**Tech Stack:** Windows batch, CMake presets, Markdown.

---

### Task 1: Add launcher

**Files:** Create `start-backend.bat`.

- [ ] Test the pre-existing missing-script state: `Test-Path -LiteralPath .\start-backend.bat` should be `False`.
- [ ] Create a batch file using `%~dp0` for the repository root and `setlocal`; probe `build\dev\backend\Debug`, `build\test\backend\Debug`, `build\debug\backend\Debug`, and `build\release\backend\Release` for `anime_vault_server.exe` in that order. If none exists, print the two `dev` CMake commands and exit 1. Otherwise `pushd "%~dp0"`, invoke `"%SERVER%"`, capture `%ERRORLEVEL%`, `popd`, and exit with the captured code.
- [ ] Test missing-binary behavior with a disposable copy of the batch file in an empty temporary directory; expect exit 1 and the build hint. Test selection using the existing `build/debug` executable without touching real media directories by checking path resolution and launching with disposable paths and a non-default port, then stop that process.

### Task 2: Document startup

**Files:** Modify `README.md`.

- [ ] In “编译与启动”, replace the manual executable-path instruction with `start-backend.bat` after `cmake --preset dev` and `cmake --build --preset dev`. State the search order, foreground behavior, default `8848`, environment-variable inheritance, and that qB credentials must remain outside the batch file.
- [ ] Verify `git diff --check` and compare README instructions against the batch file.

### Task 3: Repository verification

**Files:** no further files.

- [ ] Run CMake configure/build, CTest, frontend tests, lint, and production build as required by `AGENTS.md`. Report exact outcomes and any unavailable prerequisite.
