# Windows Backend Build Batch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Windows backend by running one root-level batch file.

**Architecture:** `build-backend.bat` validates prerequisites, changes to its own directory, configures and builds the `dev` CMake preset, and returns the first failure code. README documents build and separate launch.

**Tech Stack:** Windows batch, CMake presets, Markdown.

---

### Task 1: Batch wrapper

**Files:** Create `build-backend.bat`.

- [ ] Confirm `Test-Path .\build-backend.bat` is false before adding the feature.
- [ ] Add `@echo off`, `setlocal`, checks for `VCPKG_ROOT`, `%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake`, and `cmake` via `where cmake`. Use `pushd "%~dp0"`, `cmake --preset dev`, then `cmake --build --preset dev`. On each failure, capture `%ERRORLEVEL%`, restore the original directory with `popd` when applicable, and `exit /b` with that code. Do not call `start-backend.bat`.
- [ ] From another working directory, run the script without `VCPKG_ROOT` and expect nonzero exit with a useful message. Set `VCPKG_ROOT=D:\av-build-cache\vcpkg` and run it again; expect exit 0 and `build\dev\backend\Debug\anime_vault_server.exe`.

### Task 2: README

**Files:** Modify `README.md`.

- [ ] Replace the daily `cmake --preset dev` and `cmake --build --preset dev` snippet with `.\build-backend.bat`, then `.\start-backend.bat`. Explain that the build script requires `VCPKG_ROOT`, runs configure and build, and does not start the server. Keep the existing manual commands in the prerequisite/test section.
- [ ] Run `git diff --check` and compare the documentation with the batch file.

### Task 3: Verification

**Files:** no further files.

- [ ] Run CMake configure/build, CTest, frontend tests, lint, and production build as required by `AGENTS.md`. Record exact outcomes.
