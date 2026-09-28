# Bangumi page configuration design

## Scope

Move all Bangumi configuration needed by Anime Vault's current public search, subject binding, and cover scraping into Settings. These operations use public Bangumi APIs; do not add personal access-token or OAuth flows until the application implements account-specific features. Keep the upstream API and image hosts fixed and HTTPS-only.

## User-facing behavior

The Bangumi section displays a User-Agent input, configuration source (`saved`, `environment`, or `none`), Save, Test connection, and Clear saved configuration. The initial suggested value is `suoyi127/Link-to-Bangumi/0.1 (Windows) (https://github.com/suoyi127/Link-to-Bangumi)`. The app recommends including developer ID, application/version, and project homepage, but accepts other printable valid User-Agent values. Testing the draft performs a live public subject request without saving. Saving activates search, binding, and cover requests immediately; clearing returns to the legacy environment value if one exists. The UI distinguishes an unconfigured client from a network or HTTP failure and never claims that a cached search proves connectivity.

## Data and runtime boundaries

Persist the non-secret User-Agent as a dedicated key in the existing `setting` table. No schema column is added. Validate maximum length and reject CR/LF, NUL, and other control characters before storage or HTTP use. A long-lived runtime adapter implements both `BangumiTransport` and `CoverImageFetcher`, taking a thread-safe snapshot of the active HTTPS transports for each request. This keeps existing `BangumiService` and `CoverScraper` references stable while changing the User-Agent live. In-flight requests finish with their previous snapshot.

Expose `GET/PUT/DELETE /api/bangumi/config` and `POST /api/bangumi/config/test` with explicit request/response DTOs, JSON validation, request-size limits, and stable error codes. `GET /api/settings` reports the runtime's current `bangumiConfigured` value, not a startup snapshot. The test route calls Bangumi directly instead of the caching service. It returns a small status object and does not include remote response bodies or user-supplied headers in errors.

## Verification

Use disposable SQLite for persistence and API tests, with fake transports for hot-swap behavior. Add focused frontend tests for load, save, test, and clear. Run the repository-required CMake configure/build/CTest and frontend tests/lint/production build once for this milestone. A manual live test may call the public Bangumi endpoint but must not change the user's media or Bangumi account.
