# Portable runtime and user-selected qB directory

## Context and scope

The current server defaults to `D:/追番`, requires the qB source directory to exist during construction, and exposes the effective paths as read-only settings. This prevents a clean Windows installation from opening without that drive or qBittorrent. This subproject makes the existing React/C++ application start without those prerequisites and lets the user choose the qB download directory. Desktop hosting and the installer are separate subsequent subprojects.

## Recommended design

The C++ backend owns per-user runtime configuration. `ANIME_VAULT_DATA_DIR` remains a developer override; otherwise application data lives under `%LOCALAPPDATA%/AnimeVault`. The default import and library directories are separate siblings under the user's Videos directory, with a per-user fallback when Videos cannot be resolved. They are created only when needed. The qB source starts *unconfigured* and is never silently inferred from `D:/追番` or another directory. Existing explicit environment overrides remain supported.

The settings API exposes the configured qB directory, a `qbDownloadConfigured` flag, and whether an explicit environment override is active. A dedicated validated write endpoint stores a user-selected absolute local directory in the existing settings store; it refuses writes while `ANIME_VAULT_SOURCE_DIR` overrides that choice. It rejects nonexistent or nondirectory paths, drive/share roots, overlap or containment with import/library/data roots, and unsupported path encodings. The endpoint never creates, moves, deletes, or scans files. It reports that a backend restart is required. When the desktop host exists, it performs that restart; during development the user restarts `dev.ps1`. Changing the directory does not rewrite historical media records or move old files. Existing records under the old root remain visible, but file actions continue to apply the current-root containment rules and may be unavailable until the old root is selected again.

Without a configured qB directory, the backend uses an empty private sentinel directory solely to satisfy existing constructor contracts. All qB-source scan and Mikan rule/feed creation entry points explicitly reject the operation with `qb_download_dir_unconfigured` before they reach that sentinel. Import scanning, library browsing, Bangumi, and settings remain available. A selected qB directory is a *candidate* qB-owned source, not proof each file belongs to a qB torrent; existing qB completion checks and no-move/no-delete restrictions remain in force. The external import root never becomes a qB source.

## Interfaces and ownership

- A focused runtime-path module resolves per-user defaults, environment overrides, persisted qB source, and validates source changes. It has no Drogon dependency.
- `main.cpp` composes resolved paths and passes `qbDownloadConfigured` to the API. The qB Web UI connection is optional; configuring a directory alone does not claim qB is connected.
- The settings controller validates and persists path changes through explicit DTOs and stable error codes. The existing preference PUT remains backward compatible.
- The React settings page displays the unconfigured state, accepts a directory path, calls the dedicated endpoint, and shows the restart requirement. It disables Mikan download actions when the source is unconfigured.

## Failure and safety behavior

Startup does not touch `D:/追番`. If a configured directory later disappears, startup still opens the app with its private empty source and marks qB download scanning unavailable; the saved choice stays visible for correction. A path update is validated against canonical roots and does not alter disk content. No endpoint accepts a root directory, a relative path, or a path nested inside the import, library, or data directories. Until a saved change is activated by restart, the Mikan download endpoints reject it rather than sending new torrents to the old directory.

## Verification

Test path resolution with a disposable data directory and no qB directory; test rejection of overlapping/invalid paths and acceptance of an existing independent directory. Test that qB scans and automatic Mikan rule/feed actions reject an unconfigured source while import scanning remains usable. Test settings-page configured/unconfigured states. At the milestone run the repository-required CMake configure/build, CTest, frontend tests, lint, and production build. Never run filesystem tests on `D:/追番`.

## Next subprojects

1. Developer entry point and desktop host: `dev.ps1` starts Vite plus Debug backend; production frontend is served by the backend on loopback, and a self-contained .NET WebView2 `AnimeVault.exe` owns the backend process and opens one window. Both use the same React/C++ code.
2. Installer and Releases: build a self-contained host, backend, frontend assets, and prerequisites into an Inno Setup `Setup.exe`; add GitHub release automation and clean-machine smoke verification. The installer must not require `D:/追番` or installed qBittorrent.
