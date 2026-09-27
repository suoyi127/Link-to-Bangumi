# Anime Vault Phase 0-5 Design

## Goal

Build a Windows-first, local-only video resource manager covering phases 0-5 of the supplied specification: repository initialization, filename parsing, directory scanning, SQLite persistence, safe organization, Bangumi association, and a React management interface.

The application must not modify existing files under `D:\追番\番剧` during development or automated testing. Production organization defaults to creating hard links in `D:\追番\媒体库` only after an explicit preview and confirmation.

## Confirmed Environment

- Runtime platform: native Windows.
- Access scope: local machine only; the HTTP server binds to `127.0.0.1`.
- Source directory: `D:\追番\番剧`.
- Media library: `D:\追番\媒体库`.
- Runtime data: `D:\追番\anime-vault-data`.
- Preferred player: MPV. Playback is outside phases 0-5, but settings schema may reserve its configuration.
- qBittorrent runs on the same machine, but its Web UI/API is currently disabled.
- qBittorrent integration is optional and disabled until configured. Directory scanning remains fully usable.
- The existing Python proxy bridge serves only qBittorrent RSS/Misc traffic. Anime Vault never connects to ports 7890 or 7897.
- Bangumi API traffic connects directly to the Internet.

## Delivery Strategy

Use a modular monolith and complete a vertical slice at each milestone:

1. Repository, build, health endpoint, frontend shell, and test harness.
2. Pure filename parser and a fixture set with at least 50 cases.
3. Directory scan, SQLite migrations, inbox API, path planning, and conflict detection.
4. Transactional hard-link, symbolic-link, and copy executor with idempotency and audit logs.
5. Bangumi search, candidate ranking, cache, retry state, and manual binding.
6. Dashboard, inbox, library, detail, and settings pages that exercise the completed APIs.

Each milestone must build and pass its tests before the next begins.

## Architecture

The repository contains a C++20/Drogon backend and a React/TypeScript frontend. Core business logic is compiled into the `anime_vault_core` static library. Drogon controllers translate HTTP requests into application commands but contain no path, parsing, matching, or file-operation rules.

The backend is divided into:

- Domain: `EpisodeNumber`, parse results, anime/media entities, organization plans, confidence and status values.
- Application services: scanning, inbox correction, organization preview/execution, and Bangumi matching.
- Ports: repository, filesystem, clock, qBittorrent, Bangumi, and process interfaces.
- Infrastructure adapters: SQLite, Windows/filesystem operations, Drogon HTTP client, and the optional qBittorrent Web API.
- API: DTO validation, JSON serialization, stable error mapping, and rate limiting.

The frontend submits user intent only. It never constructs trusted filesystem paths or performs file lifecycle decisions.

## Runtime Data Flow

### Scan and Parse

The directory scanner enumerates supported video extensions below `D:\追番\番剧`, excludes temporary extensions such as `.part` and `.!qB`, and ignores non-video attachments. A stability check compares size and modification time across a configurable window before admitting a file.

The parser runs a staged pipeline:

1. Remove the extension and normalize Unicode, whitespace, and separators.
2. Extract release group and technical tags.
3. Detect episode syntax by ordered rules.
4. Derive the candidate title and season.
5. Match local aliases.
6. Compute confidence and warnings.

An `EpisodeNumber` stores an integer and an optional decimal component; it never uses floating point and serializes to text. Episode types are `normal`, `sp`, `ova`, `ncop`, `nced`, and `unknown`.

Confidence policy:

- `>= 0.85`: eligible for automatic organization only if the user enables it later.
- `0.60 <= confidence < 0.85`: requires confirmation.
- `< 0.60`: parse failure; no file plan may execute.

The fixture set includes the three currently observed real filenames, stored as filenames only, plus synthetic Chinese, Japanese, English, season, batch, double-episode, special, decimal, version, and missing-episode cases to reach at least 50 samples.

### Inbox and Organization

Every newly discovered file enters the inbox. A user may correct its title, season, episode, type, or Bangumi binding. Preview generates a persisted, expiring `plan_id` with source identity, normalized target, operation, expected metadata, and conflicts.

Execution revalidates:

- the plan is unexpired and unexecuted;
- the source exists, is stable, and matches the planned size and modification state;
- source and target resolve within their allowed roots;
- no target path exists;
- qB state allows the selected operation when the qB adapter is available.

When qB is unavailable, the application permits only hard-link, symbolic-link, and copy operations. It never moves or deletes source files.

The executor creates a temporary link or copy, validates its size, and atomically renames it to the final target. Copy mode may optionally hash the result. Failures remove temporary artifacts and record a sanitized audit event. Repeating the same idempotency key returns the original result and creates nothing new.

Hard link is the default because the source and library are on drive D. Symbolic link and copy remain user-selectable fallbacks. Existing targets are never overwritten. Multiple versions of the same episode receive distinguishable names such as `01 [1080p HEVC].mkv`.

### Bangumi

The matcher first checks manual bindings and normalized aliases. Otherwise it queries the official Bangumi API directly with an identifiable User-Agent, timeout, bounded retry policy, local rate limiter, and SQLite cache.

Candidates are ranked using normalized title similarity, animation type, year, and episode count. The API returns at most five candidates. Automatic binding is permitted only for a unique high-confidence result; all other cases require explicit selection. Network failure marks matching as retryable and never blocks local scanning or organization.

## Data Model

Ordered SQL migrations create these phase 0-5 tables:

- `anime`: display/original titles, season, year, Bangumi subject, cover, lock flag, timestamps.
- `anime_alias`: normalized aliases and their source.
- `media_file`: source/library paths, filename, exact text episode, type, size, link mode, confidence, status, optional torrent hash, timestamps.
- `scan_job`: source, status, progress counts, error summary, timestamps.
- `organize_plan`: plan identity, source snapshot, target, operation, expiry, execution state, idempotency key, timestamps.
- `organize_job`: execution result and sanitized failure details.
- `bangumi_cache`: query key, response JSON, expiry, retry metadata.
- `setting`: non-secret JSON settings.
- `audit_log`: action, entity reference, sanitized details, timestamp.

Watch and cleanup tables are deferred until phases 6 and 7. Player settings may be represented as non-secret settings, but playback controls are not exposed in the phase 0-5 UI.

Schema changes always use a new ordered migration and update the schema version. qB credentials and future API tokens come only from environment variables and are never persisted or logged.

## HTTP API

Implement the phase 0-5 subset:

- `GET /health`
- `POST /api/scans`
- `GET /api/scans/{id}`
- `GET /api/inbox`
- `POST /api/inbox/{id}/parse`
- `POST /api/organize/preview`
- `POST /api/organize/execute`
- `GET /api/anime`
- `GET /api/anime/{id}`
- `GET /api/bangumi/search?q=`
- `PUT /api/anime/{id}/bangumi`
- `GET /api/settings`
- `PUT /api/settings`
- `GET /api/audit-logs`

Mutating DTOs receive explicit validation. Organization execution requires both `plan_id` and an idempotency key. Error responses contain a stable application code, a user-safe message, optional field errors, and a request ID. Internal paths and secrets are omitted unless required for a local preview response.

## User Interface

Use a desktop-oriented Ant Design application shell with a left navigation rail and a compact status header.

- Dashboard: discovered files, pending confirmation, library titles, recent scan status, recent failures, and a scan action.
- Inbox: source item list, editable parse fields, confidence/warnings, Bangumi candidates, target preview, conflicts, and confirmation actions.
- Library: cover grid/list toggle, title metadata, episode/version list, and Bangumi link.
- Anime detail: aliases, binding controls, files, organization status, and audit history.
- Settings: source/library/data paths, organization mode, scan interval, optional qB connection, MPV executable reservation, Bangumi connectivity, and connection diagnostics.

Unavailable playback, watch-state, and cleanup actions are not displayed. The interface uses Chinese labels initially while keeping translation-friendly message keys.

## Error Handling and Safety

- Resolve and canonicalize every filesystem path, then prove it is under an allowed root.
- Pass process arguments as arrays; never build shell command strings.
- Never overwrite a target or delete a source.
- Keep development and automated file-operation tests in temporary sandbox directories.
- Use SQLite transactions for plan/job/audit state transitions.
- Recover expired or interrupted plans deterministically on startup.
- Redact passwords, tokens, cookies, private RSS URLs, and authorization headers from logs and errors.
- Apply bounded request sizes and rate limits to scan, Bangumi, and organization endpoints.
- Bind only to `127.0.0.1` in the delivered Windows preset.

## Testing

Backend tests use Catch2 and CTest:

- Parser parameter tests cover at least 50 filenames and confidence boundaries.
- Domain tests cover exact episode parsing/serialization and target naming.
- Filesystem integration tests run in temporary directories and cover conflicts, missing sources, Unicode, duplicate execution, rollback, and cross-volume hard-link failure handling.
- SQLite integration tests apply migrations from an empty database and verify repository and transaction behavior.
- API tests verify validation, error codes, idempotency, and local-only defaults with fake external adapters.
- Bangumi tests use a fake transport for ranking, cache, timeout, retry, and unavailable-service behavior.

Frontend tests use Vitest and Testing Library for forms, states, and API error presentation. Playwright covers the critical mocked flow: scan, correct, preview, confirm, and view the organized episode. No end-to-end test writes to `D:\追番`.

Completion verification for each milestone includes CMake configure/build, CTest, frontend test, frontend lint, and production frontend build. The final phase 0-5 verification also launches both services, checks `/health`, and exercises the critical browser flow against a disposable database and filesystem sandbox.

## Out of Scope

- Streaming or transcoding.
- Playback execution and MPV progress IPC.
- Watch-state tracking.
- Cleanup candidates, trash, restore, or permanent deletion.
- Authentication, HTTPS, or LAN exposure.
- Enabling or reconfiguring qBittorrent Web UI.
- Modifying the existing qBittorrent proxy bridge.
- Any automatic source-file movement or deletion.

## Acceptance Criteria

1. A clean checkout configures and builds with the documented Windows presets.
2. Backend and frontend start locally; `/health` succeeds.
3. The parser fixture suite has at least 50 cases and enforces confidence policy.
4. Directory scanning discovers stable supported videos without changing source files.
5. Users can correct metadata, bind Bangumi, preview an organization plan, and explicitly execute it.
6. Hard-link organization is transactional, idempotent, audited, and never overwrites a target.
7. Bangumi failure does not block local features.
8. The Web UI completes the scan-to-library flow without command-line interaction.
9. All tests, lint checks, and production builds pass using disposable test data.
