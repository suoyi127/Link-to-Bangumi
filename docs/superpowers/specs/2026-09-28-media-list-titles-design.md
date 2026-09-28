# Media list titles in anime details

The anime detail page currently renders each media file's filename, metadata, and full source path. Replace the path-focused presentation with a human-readable label without changing the API or stored media records.

For each row, render `media.title` as the primary title; if it is blank, fall back to the anime's `displayTitle`, then the filename. Append `[episodeNumber]` only when the episode number is nonblank. Keep the original filename in secondary text alongside status/origin so users can still distinguish releases. Do not render the full source path in this list. Keep playback by media ID, pagination, binding, and all other behavior unchanged.

Test a normal row with a distinct title and path: the title plus episode is visible, filename remains visible, and path text is absent. Also test a blank media title/episode fallback. Existing detail-page tests and the repository milestone checks must remain green.
