# Dual-source title enrichment

The media library currently derives Chinese names from Mikan RSS bilingual titles, then searches Bangumi v0 by the resulting display title. A romanized media title without a matching Mikan entry cannot reliably be found by the v0 search API. Bangumi subject details contain an `infobox` “别名” field, while the legacy subject search indexes some of those aliases.

## Approaches

1. **Recommended: Mikan first, verified Bangumi fallback.** Preserve RSS enrichment. For still-unbound titles, search Bangumi's alias-aware endpoint, fetch the sole candidate's v0 details, and verify its name, Chinese name, or alias against the original title before binding. This adds a few requests only for unresolved titles and does not invent translations.
2. Import the entire Bangumi catalog into a local alias index. This could improve offline matching but adds a large synchronization and freshness burden.
3. Fuzzy-search v0 names only. This is simpler but cannot resolve aliases that v0 search does not index.

## Behavior

The existing Mikan step remains first and authoritative for an untouched local title. Bangumi fallback is only attempted after normal search cannot auto-bind. It uses the official legacy search to discover candidate IDs, then v0 subject details to verify aliases. A candidate is eligible only when it is the unique search result, is an animation, and the normalized local title exactly matches a subject name/alias or has a sufficiently long near-exact romanized match. Short or ambiguous results remain unbound. The fallback may retry once with a distinctive prefix when a long romanized title has no exact search result. Request limits and errors leave the library untouched.

On verified binding, use Bangumi's Chinese name as the display title only when the local title is still untouched and unlocked. Preserve the romanized title as a Bangumi alias. Do not change source filenames or move files. Mikan and Bangumi data remain independent paths; neither requires the other to be configured.

## Verification

Focused tests cover the live failure shapes (`Saijo no Osewa`, `Toumei ... o/wo Shita`), ambiguity rejection, and preservation of user-edited titles. Run the backend build and focused tests. Existing integration tests protect normal search and binding.
