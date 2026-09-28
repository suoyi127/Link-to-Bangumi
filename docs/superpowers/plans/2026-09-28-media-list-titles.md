# Media List Titles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show media titles rather than full file paths in the anime detail media list.

**Architecture:** Change only the React detail view, using existing `Media.title`, `Media.episodeNumber`, and `AnimeDetail.displayTitle`. Preserve filename and metadata as secondary context, remove the `sourcePath` text node, and leave API/playback behavior alone.

**Tech Stack:** React, TypeScript, Ant Design, Vitest, Testing Library.

---

### Task 1: Render a readable media label

**Files:** Modify `frontend/src/components/AnimeDetailPage.test.tsx` and `frontend/src/components/AnimeDetailPage.tsx`.

- [ ] Add a test rendering a media row with `title: '规范番剧名'`, `episodeNumber: '03'`, filename `release.mkv`, and sourcePath `C:\downloads\release.mkv`. Assert `规范番剧名 [03]` and `release.mkv` are visible, while the full source path is not.
- [ ] Add a test with blank `title` and `episodeNumber`. Assert the anime's `displayTitle` is used with no empty brackets.
- [ ] Run `npm.cmd --prefix frontend test -- --run src/components/AnimeDetailPage.test.tsx` and observe the expected failure before changing the component.
- [ ] Compute one display label per row using `item.title.trim() || detail.displayTitle.trim() || item.filename`, append the trimmed episode only if nonempty, render it as primary text, render filename in secondary text, and remove the `sourcePath` node.
- [ ] Rerun the targeted test and confirm it passes.

### Task 2: Verify and commit

- [ ] Run `cmake --preset test`, `cmake --build --preset test`, `ctest --preset test`, `npm.cmd --prefix frontend test -- --run`, `npm.cmd --prefix frontend run lint`, `npm.cmd --prefix frontend run build`, and `git diff --check`.
- [ ] Review only changed files and commit. Preserve the current worktree and generated installer artifacts.
