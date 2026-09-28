# qB Web UI page configuration design

## Goal

Make the Settings page sufficient to configure and verify the local qBittorrent Web UI connection without editing environment variables or restarting Anime Vault. The qB download directory remains a separate setting.

## Decision

Use a dedicated qB connection form and API. Keep credentials out of the ordinary preferences API. Persist the URL, username, and password together in a Windows CurrentUser-protected credential record scoped to Anime Vault's data directory. An existing environment-variable connection remains a fallback only when no saved record exists. This is preferred to plaintext SQLite storage, and to a launcher-only configuration that would not meet the page-configurable goal.

## Contract and behavior

- The form has URL (default `http://[::1]:8080`), username, password, Save, Test connection, and Clear saved configuration. A blank password on an edit retains the saved password; it is never returned to the browser.
- `GET /api/qb/config` returns URL, username, `configured`, and `source` (`saved`, `environment`, or `none`), never a password. `PUT /api/qb/config` accepts the three fields, validates them, saves the protected record, and activates a new client immediately. `DELETE` removes the saved record and reactivates the environment fallback. `POST /api/qb/config/test` checks draft credentials without saving them and returns the existing qB status shape.
- Only explicit HTTP loopback endpoints (`127.0.0.1` or `[::1]`) with a valid port are allowed. No userinfo, path, query, fragment, control characters, or DNS names. The network client connects to the validated address and port and performs its preflight before login; credentials are never sent to the IPv4:8080 service accidentally.
- All qB status and RSS operations use a snapshot of the current client, so a save switches subsequent requests immediately while in-flight requests can finish safely. The settings page refreshes status and RSS availability after a save or clear.
- Save does not require a successful qB connection. Test provides specific unreachable/authentication/unexpected-service feedback, so a temporarily stopped qB does not prevent preserving settings. Empty password is only valid when retaining a previously saved password.
- The ordinary preference field `qbWebUiUrl` is legacy and has no effect on the active client. Remove its edit control from the UI; keep the existing API field temporarily for compatibility.

## Security and failure handling

The protected record is readable only by the same Windows user on the same machine. Do not log credentials or include them in settings, status, error, or audit JSON. Credential-read/decryption errors produce a stable configuration error and do not silently fall back to a different service. Validate the URL again on load. The local API continues to bind to loopback; all mutating credential endpoints accept JSON only and reject oversized or malformed bodies.

## Verification

Test URL validation, protected-store round trip/clear with a disposable test key, credential-free responses, retain-password behavior, immediate client replacement, and the Settings form. Run the repository-required CMake configure/build/CTest and frontend test/lint/build once for this milestone. Use a manual localhost qB connection check after implementation; never mutate or delete torrents during testing.
