# Bangumi cover scraping design

The media library already stores a Bangumi cover URL when a subject is bound. This feature makes covers durable and allows missing covers on bound anime to be refreshed.

Use the official Bangumi subject API to obtain the image URL. Never infer a subject from a cover or scrape HTML. Only accept HTTPS images hosted at `lain.bgm.tv`; fetch with a fixed-host client, a timeout, a byte limit and JPEG/PNG/WebP validation. Store the bytes under the application's data directory and expose them through a same-origin read-only cover route. Cache files are named by anime and bound subject IDs, so a rebind cannot serve a previous subject's image. Update `cover_url` only after a complete cache write and only if the binding is still the same. A failed refresh leaves the previous cover untouched.

On Windows, outbound Bangumi API and cover requests use WinHTTP's automatic current-user system proxy discovery. This is normal Internet routing and is independent of the qB RSS proxy bridge; both hosts remain fixed and HTTPS certificate validation stays enabled.

An explicit refresh action on the anime detail page handles existing bound entries with missing or broken covers. Newly bound anime use the same scraping service after binding, without making a successful binding depend on image availability. The media library continues to show its placeholder if no valid image exists. Bangumi network access requires the configured identifiable User-Agent; when absent, the refresh action reports the existing `bangumi_unconfigured` error. No downloads, imports, or media files are changed.

Tests use a fake Bangumi/image transport and disposable data directories. They cover URL allowlisting, image validation, successful cache/write/serve metadata, subject rebind races, and failure preserving the old URL. Frontend tests cover refresh feedback and displayed image state. Do not use live Bangumi in automated tests.
