# Windows installer and release handoff

## Outcome

Keep the developer path unchanged. A release build creates `Setup.exe` for x64 Windows; installing it yields one launchable `AnimeVault.exe` plus private backend/web resources. No qB installation, qB directory, `D:\追番`, Git, Node, vcpkg, or .NET SDK is required on the target PC.

## Packaging and prerequisites

Use the existing self-contained desktop package as input to Inno Setup 7. The setup binary includes the official Microsoft Visual C++ x64 Redistributable and WebView2 Evergreen bootstrapper. During installation, detect the runtime registrations; install a missing prerequisite before copying the app, and fail with an actionable error if an installer fails. WebView2 bootstrapper requires internet on a machine without WebView2; the separate `AnimeVault.exe` remains capable of showing a missing-runtime message. Download prerequisite binaries only from Microsoft endpoints during the build and verify Microsoft Authenticode signatures before packaging. A setup produced by this workflow is unsigned until a code-signing certificate is supplied; document SmartScreen implications rather than pretending it is signed.

Install under Program Files, create Start Menu and optional desktop shortcuts, preserve LocalAppData database/media on uninstall, and never hardcode qB credentials or media paths. The installer input is an explicitly selected freshly built package directory, not an arbitrary workspace root. Release files are generated under ignored `build/` and prior output is not overwritten.

## Release automation

A local build script takes a package directory and `ISCC.exe` path, checks exact required layout, downloads/verifies prerequisites, and invokes the installer compiler to a unique output directory. A tag-triggered Windows GitHub Actions workflow runs the C++/frontend/.NET packaging build, invokes the installer build, and uploads the resulting `Setup.exe` to GitHub Releases. Secrets are not required to build the unsigned installer. Do not push a tag automatically from the local workspace.

## Verification boundary

Compile the `.iss` script locally if Inno Setup is available; smoke-run the installer on this machine without changing actual media or qB settings only if a disposable install destination is supported. A genuinely clean Windows VM remains a separate acceptance test. Continue repository-required CMake/CTest/frontend checks at the milestone.
