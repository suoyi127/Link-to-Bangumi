CREATE TABLE anime_merge_preview (
 id TEXT PRIMARY KEY,
 source_id INTEGER NOT NULL,
 target_id INTEGER NOT NULL,
 subject_id INTEGER NOT NULL,
 snapshot TEXT NOT NULL,
 expires_at INTEGER NOT NULL,
 completed INTEGER NOT NULL DEFAULT 0 CHECK(completed IN (0,1))
);
CREATE INDEX anime_merge_preview_expiry ON anime_merge_preview(expires_at);

-- 删除合并源后仍保留身份上界，避免旧异步回包命中新建条目。
CREATE TABLE anime_identity (singleton INTEGER PRIMARY KEY CHECK(singleton=1), high_water INTEGER NOT NULL);
INSERT INTO anime_identity VALUES(1, (SELECT COALESCE(MAX(id),0) FROM anime));
CREATE TRIGGER anime_identity_insert AFTER INSERT ON anime BEGIN
 UPDATE anime_identity SET high_water=MAX(high_water,NEW.id) WHERE singleton=1;
END;
