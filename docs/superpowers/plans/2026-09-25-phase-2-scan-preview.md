# Phase 2 Scan, Persistence, and Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Keep one meaningful test for each safety boundary; the user requested minimum testing.

**Goal:** Discover stable video files under the configured source root, persist inbox records in SQLite, and generate conflict-aware organization previews without changing media files.

**Architecture:** Framework-independent scanner and planner services use narrow filesystem and repository ports. SQLite and Drogon adapters live outside `anime_vault_core`; the server composes them. Phase 2 never creates, moves, or deletes media files.

**Tech Stack:** C++20, `std::filesystem`, SQLite C API through vcpkg, Drogon, Catch2, CMake.

---

## File Map

- `backend/include/anime_vault/domain/MediaFile.hpp`: media identity, parse and inbox status.
- `backend/include/anime_vault/services/DirectoryScanner.hpp` and matching source: root-bound video enumeration and stability policy.
- `backend/include/anime_vault/services/TargetPlanner.hpp` and matching source: safe target naming and conflict detection.
- `backend/include/anime_vault/repositories/MediaRepository.hpp`: storage port.
- `backend/src/infrastructure/database/SqliteDatabase.cpp` and matching header: connection, migration, transactions.
- `backend/src/infrastructure/database/SqliteMediaRepository.cpp` and matching header: persistence.
- `backend/migrations/001_initial.sql`: ordered schema.
- `backend/include/anime_vault/api/MediaController.hpp` and matching source: scan, inbox, preview routes.
- `backend/tests/unit/DirectoryScannerTest.cpp`, `TargetPlannerTest.cpp`: minimum source-boundary and conflict tests.
- `backend/tests/integration/SqliteRepositoryTest.cpp`: migration/persistence smoke test.

## Task 1: Source Scanner

- [ ] Write a failing test using a temporary tree containing `show/01.mkv`, `show/01.part`, and an outside file. Assert only the stable MKV inside the root is returned. Test the real filesystem.
- [ ] Run the focused test. Expected RED: scanner API absent.
- [ ] Implement:

```cpp
struct SourceFile {
    std::filesystem::path path;
    std::uintmax_t size{};
    std::filesystem::file_time_type modifiedAt{};
};
class DirectoryScanner {
public:
    explicit DirectoryScanner(std::filesystem::path root);
    std::vector<SourceFile> scan(std::chrono::seconds stableFor,
                                 std::filesystem::file_time_type now) const;
};
```

Resolve root and entries; require regular supported video files within root and older than the stability window. Ignore `.part` and `.!qB`. Surface permission errors. No media writes.
- [ ] Run the focused test and compiler build. Expected GREEN. Commit `feat: scan stable local videos`.

## Task 2: Target Preview Planner

- [ ] Write a failing test: title `Moonbound`, episode `1`, MKV produces `Moonbound/01.mkv`; an existing target reports conflict; traversal title is rejected. Use a temporary root.
- [ ] Run focused test. Expected RED: planner API absent.
- [ ] Implement:

```cpp
struct TargetPlan {
    std::filesystem::path source;
    std::filesystem::path target;
    std::string mode{"hardlink"};
    std::vector<std::string> conflicts;
};
class TargetPlanner {
public:
    explicit TargetPlanner(std::filesystem::path libraryRoot);
    TargetPlan preview(const SourceFile& source, std::string_view displayTitle,
                       EpisodeType type, const EpisodeNumber& episode,
                       std::string_view versionLabel = {}) const;
};
```

Reject traversal, reserved Windows names, illegal path characters, trailing dots/spaces, target collisions, and unsupported extensions. Normal episodes pad whole numbers to two digits; decimals retain their fractional digit; specials use `SP01`, `OVA01`, `NCOP01`, or `NCED01`. Version labels distinguish variants and never overwrite.
- [ ] Run focused test and compiler build. Expected GREEN. Commit `feat: preview safe media targets`.

## Task 3: SQLite Schema and Repository

- [ ] Add `sqlite3` to `vcpkg.json`. Write a failing temporary-database test that migrates an empty DB, inserts a scan/media record, reopens, and reads it. Assert schema version `1`.
- [ ] Run focused test. Expected RED: repository/schema absent.
- [ ] Implement `SqliteDatabase::migrate()` with transactional ordered SQL; set `PRAGMA foreign_keys=ON` and `PRAGMA user_version=1`. Migration creates `anime`, `anime_alias`, `media_file`, `scan_job`, `organize_plan`, `organize_job`, `bangumi_cache`, `setting`, and `audit_log` with approved columns and unique constraints. `episode_number` is TEXT. Repository writes use prepared statements.
- [ ] Run focused test. Expected GREEN. Commit `feat: persist scan inbox in sqlite`.

## Task 4: Scan, Inbox, and Preview API

- [ ] Write one failing route contract test with a disposable source tree and database: `POST /api/scans` creates a scan ID, `GET /api/inbox` exposes a parsed file, and `POST /api/organize/preview` returns a target/conflict without creating it.
- [ ] Run focused test. Expected RED: routes absent.
- [ ] Implement `POST /api/scans`, `GET /api/scans/{id}`, `GET /api/inbox`, `POST /api/inbox/{id}/parse`, and `POST /api/organize/preview`. Store source snapshots and parser results. Validate manual corrections. Preview persists an expiring plan ID. Execution remains unavailable in phase 2. DTO errors use stable codes and HTTP 400/404/409. Rate limit scan requests.
- [ ] Run route contract, core tests, and backend build if dependencies are available. Expected GREEN. Commit `feat: expose scan inbox and preview api`.

## Gate

- [ ] Run `cmake --preset test`, `cmake --build --preset test`, and `ctest --preset test --output-on-failure`; report exact status. If the official vcpkg download remains blocked, use focused standalone builds where possible and state the integration limitation.
- [ ] Verify an HTTP preview against disposable media files; compare file listing before and after to prove zero media changes.
- [ ] Review allowed-root enforcement, target conflicts, and secret-free logs. No source movement, hard-link creation, or deletion is permitted in this phase.
