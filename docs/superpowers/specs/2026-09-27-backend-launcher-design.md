# Windows backend launcher design

## Scope

Add a root-level `start-backend.bat` that launches an already-built local backend and document it in `README.md`. `READMA.md` in the request refers to the repository's existing `README.md`.

## Behavior

- Resolve paths relative to the batch file, so invoking it from another directory works.
- Use the first existing server executable in this order: `build/dev/backend/Debug`, `build/test/backend/Debug`, `build/debug/backend/Debug`, then `build/release/backend/Release`.
- If none exists, print the `cmake --preset dev` and `cmake --build --preset dev` commands and exit with a nonzero status. Do not build implicitly.
- Run the executable in the foreground, preserve its exit code, and inherit environment variables from the launching shell. Never embed qB credentials.
- Keep the existing default port and paths in the backend. README explains `ANIME_VAULT_PORT` and optional Bangumi/qB variables without changing their semantics.

## Verification

Check launcher path selection and missing-binary behavior with disposable stand-ins where practical. Run the repository's required build and test checks; do not contact or alter real qB/download directories.
