# Anime library workflow design

Approved by the user on 2026-10-10 as one workflow milestone.

1. Poll qB completion every ten seconds in a background worker. Only completed upload states bypass the stability window. Checking, downloading and error states never qualify. Validate containment and exclude reparse points; read sources without moving or deleting them. Unchanged polls do not create scan records.
2. Return folder scan results without waiting for network enrichment. Enrich the current scan asynchronously. Exclude bound media from inbox pages and their totals; unresolved items remain available for manual correction.
3. Edit title, season and cover URL on the anime detail page. Edit episode number and type per media file. Preserve old and new titles as user aliases and update associated media metadata transactionally. Keep explicit Bangumi re-binding available. Do not change filesystem paths.
4. Sort all anime globally by first import month, ascending or descending (default descending), before pagination. Reuse anime.created_at, with ID as a stable same-month tiebreaker; rescans never change this timestamp. Expose importedMonth to both library views.
5. Catch2 regression coverage uses temporary databases/directories only. Verify CMake configure/build, CTest, Vitest, ESLint and production build. Preserve the pre-existing backend/CMakeLists.txt modification.

Review details: only a globally unique user alias can override a parsed season mismatch. New episodes inherit the edited title and season. Cover writes compare the expected URL with the current URL transactionally; only explicit cover refresh intentionally replaces a custom cover.
