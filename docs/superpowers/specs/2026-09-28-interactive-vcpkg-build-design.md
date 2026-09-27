# Build Batch vcpkg Input Design

## Scope

Change only `build-backend.bat` and its `README.md` startup instructions.

## Input behavior

Resolve the vcpkg root in this order: first batch argument, inherited `VCPKG_ROOT`, then an interactive prompt. The argument overrides any inherited value. Empty input or a path without `scripts\buildsystems\vcpkg.cmake` exits nonzero before CMake runs. Strip surrounding quotes from an interactively pasted path. Set `VCPKG_ROOT` only within the batch process (`setlocal`); do not persist it in Windows settings or a repository file.

The existing CMake configure/build sequence and exit-code propagation remain unchanged. The build does not start the server.

## Documentation and verification

README shows both positional-argument and prompt usage, without requiring a separate PowerShell environment assignment for ordinary builds. Check invalid and valid paths and run repository-required verification; do not touch media files.
