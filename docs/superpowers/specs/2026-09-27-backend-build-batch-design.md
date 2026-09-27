# Windows Backend Build Batch Design

## Scope

Add `build-backend.bat` at the repository root and update the build/start section of `README.md`.

## Behavior

- Resolve the repository root from `%~dp0`, so the batch file works from any current directory.
- Require a valid `VCPKG_ROOT` containing `scripts\buildsystems\vcpkg.cmake` and an available `cmake` command. Do not guess a machine-specific vcpkg location or bootstrap it.
- Run `cmake --preset dev`, then `cmake --build --preset dev`; stop immediately and return the failing command's exit code.
- Build only; launching remains the separate responsibility of `start-backend.bat`.
- README shows the one-time vcpkg environment setup, the batch command, and the separate start command.

## Verification

Check failure when `VCPKG_ROOT` is missing, then run the batch with the known local vcpkg installation. Verify the produced backend executable exists. Run the checks required by `AGENTS.md` without modifying media/download folders.
