# Windows Installer Implementation Plan

> Follow the repository milestone rules and use a disposable package/test location. A clean-machine acceptance test cannot be inferred from a developer-PC smoke.

1. Add `installer/AnimeVault.iss` for x64 installation, shortcuts, stable app identity, and VC++/WebView2 prerequisite detection and installation before files are copied. Keep user data untouched on uninstall.
2. Add `build-installer.ps1` to validate a specific portable package, fetch Microsoft-signed prerequisite binaries, find or accept `ISCC.exe`, compile a uniquely named `Setup.exe` output, and avoid overwriting old artifacts. Test invalid inputs before the happy path.
3. Add a Windows tag-release workflow that builds package and installer, runs minimum milestone checks, then publishes the artifact to GitHub Releases. Keep credentials and local media paths out of workflow files.
4. Update README for ordinary-user and developer paths, including WebView2 bootstrapper network requirement and unsigned installer warning.
5. Compile and inspect output locally if Inno is available; run CMake configure/build/CTest, frontend tests/lint/build, desktop tests/build, and `git diff --check`. Commit only after results are recorded.
