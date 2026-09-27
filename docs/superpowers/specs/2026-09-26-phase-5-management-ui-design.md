# Phase 5 Management UI Design

## Scope and choice

This phase completes the approved Windows-local phase 0–5 manager. Keep the existing React/Ant Design shell and page-state navigation; do not add a router or playback features. The alternative of introducing a router now adds dependencies and URL state without helping the four primary workflows. A static HTML rewrite would duplicate the typed API and component tests already present.

## Data and API boundary

The browser talks only to same-origin `/api` and `/health`; Vite proxies both to the loopback backend during development. A typed client turns stable backend error codes and request IDs into safe Chinese messages. Add `animeId` to media JSON so an inbox correction can open the resulting local anime without guessing by title. Add paginated `GET /api/scans` and `GET /api/audit-logs`, plus `GET /api/settings` and a bounded `PUT /api/settings` for persisted UI preferences. Runtime source/import/library/data paths and Bangumi User-Agent status are read-only effective configuration; changing them requires environment configuration and restart. Store only `preferredOperation` (`hardlink`, `copy`, `symlink`) and reserved `scanIntervalSeconds`/`mpvExecutable`/`qbWebUiUrl` preferences. The latter three are visibly marked inactive/reserved; saving them does not claim an enabled scheduler, player, or qB Web UI. Never return secrets or remote response bodies. The dashboard consumes the list APIs and has explicit qB-source and external-import scan actions.

## Screens and workflow

- Dashboard: local health, counts from paginated inbox/anime results, recent scans/failures, separate scan buttons, loading/error/empty states.
- Inbox: source and origin table, confidence and parse fields, correction form, Bangumi candidate search, local-match indicator, and a path to the anime detail. Preview is a separate action that displays operation, target and conflicts. Execution requires a visible confirmation and, for qB-origin media, a separate user attestation that qB download is complete. Reuse one idempotency key for retries of the same plan; never imply qB completion from file stability.
- Library: paginated grid/list toggle with title/season/cover and bound status. Detail pages show aliases, media with continuation, organization state, Bangumi link and explicit binding controls. Remote lookup errors leave local detail usable.
- Settings: effective paths and connectivity state, saved preferences with clear inactive labels, and recent audit entries. No playback/watch/cleanup controls.

Use state navigation inside the existing shell. Preserve keyboard-accessible Ant Design controls and Chinese labels; avoid visual assets that require external network access. Handle missing covers with a local placeholder. Frontend pagination follows `nextOffset`/`nextMediaOffset`; it never assumes an unbounded list.

## Safety and verification

Backend write DTOs are allowlisted and size-bounded. Settings writes never mutate runtime roots, qB, or the proxy bridge. Audit reads are paginated and avoid private request bodies. Frontend confirm dialogs show the exact target and origin; errors show code/request ID but do not echo remote response bodies. Tests use mocked HTTP/fake Bangumi transport and disposable filesystem/SQLite only. Per-task focused tests come first; final milestone runs CMake configure/build, CTest, frontend test/lint/build, then launches both services with disposable paths for a local health and critical-flow smoke test. No automated test writes `D:\追番`.
