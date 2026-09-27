# Phase 3 Safe Organization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox syntax for tracking. The user requested minimum testing; keep one focused real-filesystem test per safety boundary.

**Goal:** Execute a confirmed, unexpired hard-link, symbolic-link, or copy organization plan without ever moving, deleting, or overwriting a source or target media file.

**Architecture:** A framework-independent executor validates persisted plan state and current filesystem identity, creates a temporary hard link (or explicitly selected symlink/copy) within the library, verifies it, and publishes it by a no-replace operation. A service owns idempotency and audit state; Drogon only validates the request and serializes the result. No automated test writes under `D:\追番`.

**Tech Stack:** C++20, `std::filesystem`, Windows file APIs for atomic no-replace publication, SQLite C API, Drogon, Catch2, CMake.

---

## Existing contracts and invariants

- Read `AGENTS.md`, `docs/superpowers/specs/2026-09-24-anime-vault-phase-0-5-design.md`, `MediaService`, `TargetPlanner`, `SqliteMediaRepository`, and migrations 001–003 before editing.
- Also read `docs/superpowers/specs/2026-09-25-trusted-local-ingress-design.md`. The user accepted trusted local source/import directories with no hostile concurrent junction or symlink replacement; do not describe the path-only executor as safe against such an adversary. Require explicit qB completion confirmation while its API remains disabled.
- Keep the current `feature/phase-0-5` isolated workspace. Do not refactor parser/scanner/UI.
- `POST /api/organize/preview` already stores source path, size, modification timestamp, target, operation, expiry, state, and idempotency key. Never trust a path or operation supplied by the execution request; use the stored plan.
- The API must require `{ "planId": positive_integer, "idempotencyKey": nonempty_string, "confirmed": true }`. Confirmation is specific to that request and plan; no automatic execution is added.
- The request's idempotency key identifies one execution attempt and is distinct from the plan's existing preview-deduplication key. Store it on `organize_job` with a uniqueness constraint; repeated requests with the same plan/key return the original result.
- Neither implementation nor tests may operate on `D:\追番`; tests must use unique disposable source/library/data directories.
- qB integration remains unavailable: do not implement move/delete; hard link is default, and symlink/copy must be selected in an explicit preview before execution.

## File map

- Create `backend/include/anime_vault/services/OrganizationExecutor.hpp` and `backend/src/services/OrganizationExecutor.cpp`: root-bound filesystem validation and no-replace publication.
- Modify `backend/include/anime_vault/api/MediaService.hpp`, `backend/src/api/MediaService.cpp`, and `MediaController.cpp`: explicit preview mode selection; persist the selected operation.
- Create `backend/include/anime_vault/services/OrganizationService.hpp` and `backend/src/services/OrganizationService.cpp`: plan lookup, expiry, idempotency, repository state transitions, sanitized errors.
- Modify `backend/include/anime_vault/repositories/MediaRepository.hpp` and SQLite adapter: atomic claim, completion/failure, job/result lookup, audit writes.
- Create ordered `backend/migrations/005_organization_execution.sql` after migration 004 adds media provenance; update embedded migration and schema version together.
- Modify `backend/include/anime_vault/api/MediaController.hpp`, `backend/src/api/MediaController.cpp`, and `backend/src/main.cpp`: one execution endpoint and composition.
- Add `backend/tests/unit/OrganizationExecutorTest.cpp` and `backend/tests/integration/OrganizationServiceTest.cpp`: one focused test each, using disposable trees and DB.

## Task 1: No-replace hard-link executor

**Files:** Create `OrganizationExecutor.hpp/.cpp`; modify `backend/CMakeLists.txt`; test in `OrganizationExecutorTest.cpp` and `backend/tests/CMakeLists.txt`.

- [ ] Write a failing disposable-tree test: create `source/Show 01.mkv`, preview target `library/Show/01.mkv`, execute the hard-link operation, and assert both files exist, contain the same bytes, and source remains unchanged. In the same test, create the target first and assert execution returns a conflict without altering either file.
- [ ] Run only `OrganizationExecutorTest`; expected RED is missing executor API. If CMake is still unavailable, compile the focused test with MSVC/Catch2 standalone; record the exact command and result.
- [ ] Define `ExecuteFileRequest { source, target, expectedSize, expectedModifiedAt, operation }`, `ExecuteFileResult { target, bytes }`, and typed errors `invalid_root`, `source_changed`, `target_exists`, `unsupported_operation`, `publish_failed`.
- [ ] Implement constructor-time canonical validation of the configured source and library roots. At execution, require the source to be a regular non-symlink file canonically within the source root, matching stored size and modification time; reject an existing final target and any target parent that resolves outside the library root. Create missing target parents one segment at a time after validating each segment. Use an unpredictable temporary filename inside the validated target parent, create a hard link there, compare size, then publish with an OS operation that fails if the final target appeared meanwhile (Windows `MoveFileExW` without replace; portable fallback must have equivalent no-replace semantics or return unsupported). Remove only the exact temporary path created by this invocation on failure. Never remove source or final target.
- [ ] Run the focused test again; expected GREEN. Review the source and target path checks, including junction/symlink parents and the publication race. Commit `feat: execute root-bound hardlink without overwrite`.

## Task 2: Explicit symlink and copy modes

**Files:** Modify `MediaService.hpp/.cpp`, `MediaController.cpp`, and `OrganizationExecutor.cpp`; extend `OrganizationExecutorTest.cpp`.

- [ ] Write one failing disposable-tree test selecting copy mode in preview, then executing it; verify the target has the same bytes, has a different file identity from the source, and the source remains. Add a symlink case only on hosts where symlink creation is permitted; otherwise assert a stable `symlink_unavailable` error. No real media roots.
- [ ] Run the focused test; expected RED because preview ignores mode or executor rejects it.
- [ ] Define the only accepted preview `operation` values as `hardlink`, `symlink`, and `copy` (default `hardlink`). Persist the selected value in `organize_plan`; reject all others with `invalid_operation`. For a conflicting existing target, retain the selected mode and `conflict` state.
- [ ] Extend the executor: for `copy`, stream into the exact temporary path and flush/close before size verification and no-replace publication; for `symlink`, create a link at the temporary path only after revalidating the source. A symlink privilege failure returns `symlink_unavailable`. All modes use the same root/identity checks and never overwrite or remove the source.
- [ ] Run the focused test; expected GREEN. Commit `feat: preview and execute explicit link or copy mode`.

## Task 3: Persistent claim, idempotency, and audit

**Files:** Create `OrganizationService.hpp/.cpp`; modify repository port, SQLite adapter, and ordered migration 005; test in `OrganizationServiceTest.cpp`.

- [ ] Write a failing temporary-DB test: a pending, unexpired plan with matching source is confirmed once; repeating the same key returns the first result and creates no second job/link; a different key or an expired/conflicted plan is rejected. Assert the source file survives. Use only disposable directories.
- [ ] Run only `OrganizationServiceTest`; expected RED is missing service/repository methods.
- [ ] Add migration 005 with `organize_job.idempotency_key TEXT NOT NULL UNIQUE`, then a repository transaction method that claims a `pending` plan for one execution key and records a single `organize_job`. A competing claim must return the existing result for the same plan/key or `plan_already_claimed` for a different key. Use SQLite `BEGIN IMMEDIATE` plus conditional `UPDATE ... WHERE execution_state='pending'`; never hold a transaction open while performing filesystem I/O. Add completion/failure methods and a sanitized `audit_log` event. Do not alter prior migrations; update embedded migration and schema version to 5.
- [ ] Implement the service: require explicit confirmation, positive plan ID, bounded nonempty key, unexpired plan, `pending` state, and an allowed stored operation. Revalidate current source identity and target safety through the executor. On success store result and return it; on failure store a stable error code and leave source untouched. Define recovery for a process crash between claim and completion: return `execution_in_progress` and never blindly repeat the filesystem operation until a later reconciliation path can prove the target identity.
- [ ] Run the focused test; expected GREEN. Commit `feat: make organization execution idempotent`.

## Task 4: Explicit HTTP execution route

**Files:** Modify `MediaController.hpp/.cpp`, `main.cpp`, and `backend/CMakeLists.txt`; add one route assertion to the service/HTTP test only if Drogon is buildable.

- [ ] Write one failing route assertion for `POST /api/organize/execute`: missing `confirmed: true` yields HTTP 400 with stable `confirmation_required`; a valid request returns the persisted job/result ID and target path. No real media roots.
- [ ] Run the focused route test; expected RED. If Drogon cannot compile, retain the test source and report it as unrun, not passing.
- [ ] Add explicit request/response DTOs and JSON serialization. Accept only positive `planId`, bounded `idempotencyKey`, and boolean `confirmed === true`; never accept client-supplied source/target paths. Return stable HTTP 400/404/409 codes, a safe message, and request ID. Wire one long-lived service instance in `main.cpp`.
- [ ] Run the focused test; expected GREEN when dependencies are available. Commit `feat: expose confirmed organization execution`.

## Milestone gate

- [ ] Read each changed file against `AGENTS.md` and the design. Confirm no move/delete path, no overwrite flag, no shell command construction, and no operation under `D:\追番` during tests.
- [ ] Run CMake configure, build, CTest, frontend test, lint, and production build. Use the smallest focused tests; state exact unavailable prerequisites. Do not count CTest's `No tests were found` as a pass.
- [ ] On a disposable tree, compare the source file listing/bytes before and after success, conflict, and retry. Do not enable execution against real media until this focused runtime check succeeds.
