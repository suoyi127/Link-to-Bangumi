# Desktop host and developer entry point

## Scope

This subproject produces two ways to run the same React/C++ application: `dev.ps1` starts Vite plus a Debug backend for contributors, while `AnimeVault.exe` starts a bundled backend and presents the production frontend in one WebView2 window. A Windows installer and GitHub Releases automation are the next subproject, not part of this one.

## Runtime architecture

The backend serves `frontend/dist` as static files only when `ANIME_VAULT_WEB_DIR` is set to a validated absolute directory containing `index.html`. Existing `/api` and `/health` routes retain precedence. The installed host sets this variable to its bundled `web` directory, so UI and API share one loopback origin. Without this variable, development remains Vite on port 5173 proxying to the Debug backend on 8848. Static serving must not expose files above the configured web root.

The Windows host is a small self-contained .NET WinForms application using `Microsoft.Web.WebView2`. Its installed layout is `AnimeVault.exe`, `backend/anime_vault_server.exe` plus required DLLs, and `web/index.html` plus assets. On launch it validates the layout, selects an available loopback port, starts the backend as a hidden child with that port and web root, waits for its own health endpoint, and opens a WebView2 control at `http://127.0.0.1:<port>/`. It uses `%LOCALAPPDATA%/AnimeVault/WebView2` for browser profile data. Closing the window terminates its own backend process tree. If WebView2 Runtime is absent or startup fails, it displays an actionable error and stops its child; it never connects to another backend solely because a port answered. It restricts embedded navigation to its own loopback origin and opens external HTTP(S) links in the user's default browser.

The host supplies the Bangumi User-Agent containing the user-provided developer ID `suoyi127`, unless explicitly overridden. It does not bake qB credentials or a qB download path into the package. The directory remains user-configured in settings; absence of qB or `D:/追番` does not prevent launch. The installer must ensure the WebView2 Runtime later, while the host also checks it for portable/development launches.

## Developer workflow

`dev.ps1` accepts an optional vcpkg root and prompts when neither argument nor `VCPKG_ROOT` is set. It configures/builds the `dev` CMake preset, installs frontend dependencies only when missing, then launches the Debug backend and Vite on loopback with visible addresses printed in the console. It leaves existing environment path overrides intact, sets the Bangumi User-Agent when absent, and terminates both child processes on Ctrl+C or normal exit. It does not package or mutate media files.

## Verification

The static-server red test is a request to `/` against the current backend (404); after the change a disposable web root serves `index.html`, assets, and existing API/health paths, while a traversal request cannot read a sibling file. Build the WinForms project and run a host-layout/startup smoke on this Windows machine. Run repository-required CMake configure/build, CTest, frontend tests, lint, and frontend production build at the milestone. A final clean-machine installer test belongs to the next subproject.
