# Anime Vault Repository Rules

- Before each milestone, read this specification and the existing code. Work on one approved milestone at a time and finish its verification before starting the next.
- Write tests before changing any file-management behavior. Automated filesystem tests use disposable directories and never touch `D:\追番`.
- Any destructive action requires a dry-run preview and explicit user confirmation.
- Never overwrite an existing target or delete a file outside its allowed root. Resolve paths and validate containment before file operations.
- Never launch a player by constructing a shell command string; use a cross-platform process wrapper that passes the executable and argument array directly.
- Never move or delete qBittorrent sources that are unfinished, checking, or still seeding.
- Add schema fields through a new ordered SQL migration and update the schema version.
- For every API, define explicit C++ DTOs, JSON serialization, input validation, and stable application error codes.
- At every milestone, run CMake configure and build, CTest, frontend tests, frontend lint, and frontend production build; report exact results and unavailable prerequisites.
- Make no unrelated refactors.
- Use target-level CMake commands; do not set global compiler flags or include paths.
- Keep core business logic independent of Drogon controllers and other HTTP framework details so it can run independently in unit tests.
- Add concise comments at decision points, security boundaries, state transitions, and non-obvious parsing or matching rules. Do not add line-by-line narration or pursue a fixed comment density.
