# Trusted Local Ingress and Organization Design

## Decision and scope

The user accepts a local trust boundary: `D:\追番\番剧` is written only by qBittorrent, `D:\追番\外来导入` is populated by the user, and no hostile local process concurrently replaces these directories with junctions or symlinks. This narrows the threat model for phase-3 hard-link execution; it does not imply that every qB file is complete or immutable. The existing source-root, source-snapshot, target-conflict, and no-overwrite checks remain mandatory.

This document supplements `2026-09-24-anime-vault-phase-0-5-design.md`. It does not authorize moving or deleting qB sources, overwriting targets, or silently executing a preview.

## Directory roles

| Directory | Role | Who writes it | Application behavior |
| --- | --- | --- | --- |
| `D:\追番\番剧` | qB-only download source | qBittorrent | Read/scan only; never place imported files here |
| `D:\追番\外来导入` | initially empty import drop folder | user | Scan only after an explicit import action; never treat contents as qB-origin |
| `D:\追番\媒体库` | organized library | Anime Vault after confirmation | Create a new target only; never replace an existing target |
| `D:\追番\anime-vault-data` | SQLite/runtime data | Anime Vault | No media input or output |

The import folder is a sibling of `番剧`, not a child. The server reads it from `ANIME_VAULT_IMPORT_DIR` with the above local default. It must reject overlap or nesting among source, import, and library roots, including aliases through junctions. If the configured import directory is missing, the import action returns a stable configuration error; application startup does not silently create user media directories.

## Ingestion and provenance

The existing `POST /api/scans` scans only the qB source. A separate explicit import action scans `外来导入`. Both feed the same inbox and parser, but each media row records immutable provenance (`qb_download` or `external_import`) and scan origin. Re-scanning one root may update its own row, never reclassify or absorb a file from the other root. Symlink, junction, temporary, and unsupported files are rejected or ignored consistently.

The import action does not copy, move, or delete files. A file enters the inbox only after its size and modification time remain unchanged over the configured stability window. The UI labels its source and does not expose a generic path field that could read arbitrary folders.

qB Web UI/API remains disabled. Directory membership therefore expresses the user's qB-only policy, not independently verified torrent provenance or completion. For a qB-origin item, organization execution requires an explicit user confirmation that the download is complete; when a qB adapter is later enabled, its completed/checking/seeding state must be consulted before any operation that could affect the source. Current hard-link/copy operations never move or delete it.

## Organization safety within the accepted trust boundary

Preview persists a source snapshot, operation, target and conflicts. Execution requires the plan ID, an execution idempotency key, and explicit confirmation. For qB-origin items, it also requires completion confirmation while qB state is unavailable. It rechecks source containment, regular-file type, size and modification time; rejects a changed/missing source, conflicting target, expired/conflicted plan or unsupported operation; and publishes without replacing the target. Only temporary artifacts created by that execution may be cleaned on failure. The original source is never removed.

Hard link remains the default for stable files on drive D. Copy is a user-selectable fallback and avoids later source writes affecting the library; symbolic link remains optional and must disclose that it depends on its source remaining available. A hard link shares file contents with the source: if qB rewrites that file later, the library view changes too. Neither the trusted-directory assumption nor size/mtime checks prove future immutability. The UI must state this before confirmation.

## Validation and release gate

Use disposable qB-like and import trees in the minimum automated tests. Verify separate provenance, explicit import trigger, stable-file admission, missing-root error, target no-overwrite, source preservation, and retry idempotency. No test or development command writes media beneath `D:\追番`. The real execution route remains disabled until a focused end-to-end execution test passes and the approved local trust assumption is represented in the confirmation UI/API. The known vcpkg/OpenSSL build blocker must be reported rather than treated as a passing CTest run.
