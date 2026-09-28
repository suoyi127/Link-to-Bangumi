# Bangumi page configuration implementation plan

## Goal

Make the public Bangumi connection configurable in Settings without a personal token. Default User-Agent includes the project homepage. Preserve live switching and existing data.

## Tasks

1. Add focused backend tests for persisted User-Agent, validation, environment fallback, and live transport switching. Implement a thread-safe Bangumi connection manager over the existing setting table.
2. Add focused API tests for config read/save/clear/test and malformed requests. Register endpoints and make `/api/settings` reflect live configuration.
3. Add Settings UI tests for loading, saving, testing draft, and clearing Bangumi settings. Implement API client and UI controls.
4. Update README instructions and run CMake configure/build/CTest plus frontend test/lint/build. Manually verify a public Bangumi request with a disposable backend.

Keep each change scoped, use key-logic comments, and do not modify the user's media or qBittorrent configuration.
