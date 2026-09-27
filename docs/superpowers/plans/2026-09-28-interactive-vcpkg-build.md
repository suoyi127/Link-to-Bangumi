# Interactive vcpkg Build Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users supply the vcpkg root through the build command or an interactive prompt.

**Architecture:** `build-backend.bat` chooses the first batch argument, otherwise an inherited environment variable, otherwise `set /p` input. It validates the chosen path before invoking the existing CMake preset flow. README describes the two new input methods.

**Tech Stack:** Windows batch, CMake, Markdown.

---

### Task 1: Path input and validation

**Files:** Modify `build-backend.bat`.

- [ ] With `VCPKG_ROOT` unset, run `build-backend.bat "D:\av-build-cache\vcpkg"` against the old script and confirm it fails because arguments are not yet supported.
- [ ] Before the existing vcpkg-path check, set `VCPKG_ROOT` from `%~1` when an argument is supplied. If it remains undefined, prompt with `set /p "VCPKG_ROOT=Enter vcpkg root: "`. Remove surrounding quotes from pasted input, reject empty input, and keep the existing toolchain-file check. `setlocal` already prevents persistent environment changes.
- [ ] With `VCPKG_ROOT` unset, run the batch with the valid argument and expect exit 0 plus `build\dev\backend\Debug\anime_vault_server.exe`. Run it with an invalid argument and expect exit 1 before CMake. Test prompt input using a console session and expect exit 0.

### Task 2: Documentation

**Files:** Modify `README.md`.

- [ ] Replace the daily PowerShell environment assignment with `.\build-backend.bat 'D:\av-build-cache\vcpkg'` followed by `.\start-backend.bat`. State that running `.\build-backend.bat` without an argument prompts if no `VCPKG_ROOT` is inherited, and that the value is not permanently saved.
- [ ] Run `git diff --check` and compare command examples against the implemented script.

### Task 3: Repository verification

**Files:** no further files.

- [ ] Run CMake configure/build, CTest, frontend tests, lint, and production build per `AGENTS.md`; report exact results.
