# Canonical title in the media list

The anime detail API already provides the anime's canonical `displayTitle`. The same anime can contain media records whose `title` still holds a romanized release title or another recognized alias. The current detail list chooses the per-media title first, so rows for one anime display mixed languages even though the Chinese canonical title is known.

For rows in an anime detail page, show that anime's `displayTitle` first, followed by the media's episode number. If the anime title is blank, fall back to the media title, then the filename. Preserve the original filename as secondary text; do not render the full source path. Playback continues to use media ID. Do not alter SQLite titles, Bangumi bindings, alias data, or any media files. If no Chinese canonical title has been established, the existing non-Chinese title remains visible rather than guessed by transliteration.

Add a regression test with a Chinese anime `displayTitle` and romanized media `title`; verify every media row uses the Chinese title and retains its own episode number. Keep the blank-title fallback test and existing detail behavior passing.
