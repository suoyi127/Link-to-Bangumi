# Canonical Media List Title Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display the anime's established canonical Chinese title for all of its media rows, even when individual media records use a romanized alias.

**Architecture:** The detail page already has `AnimeDetail.displayTitle` and each `Media.episodeNumber`; use them only for presentation. Keep media title as a fallback when the canonical title is empty, and leave persistence and playback unchanged.

**Tech Stack:** React, TypeScript, Vitest, Testing Library.

---

### Task 1: Regression test and minimal UI correction

**Files:** Modify `frontend/src/components/AnimeDetailPage.test.tsx` and `frontend/src/components/AnimeDetailPage.tsx`.

- [ ] Add a detail-page test whose anime `displayTitle` is `与奔驰于透明之夜的你，谈一场看不见的恋爱。`, with two media items titled `Toumei na Yoru...` and episodes `03`, `04`. Assert both rendered primary labels use the Chinese title and their own episode numbers, while original filenames remain visible.
- [ ] Run `npm.cmd --prefix frontend test -- --run src/components/AnimeDetailPage.test.tsx -t "uses the anime canonical title"`; confirm it fails because the romanized media title is rendered.
- [ ] Change the label expression in `AnimeDetailPage.tsx` from `item.title.trim() || detail.displayTitle.trim() || item.filename` to `detail.displayTitle.trim() || item.title.trim() || item.filename`.
- [ ] Run the targeted test and the full `AnimeDetailPage.test.tsx` file; require both to pass.

### Task 2: Milestone verification

- [ ] Run `cmake --preset test`, `cmake --build --preset test`, `ctest --preset test`, `npm.cmd --prefix frontend test -- --run`, `npm.cmd --prefix frontend run lint`, `npm.cmd --prefix frontend run build`, and `git diff --check`.
- [ ] Commit only the scoped documentation, UI, and regression test. Preserve the existing worktree, database, media files, and running backend processes.
