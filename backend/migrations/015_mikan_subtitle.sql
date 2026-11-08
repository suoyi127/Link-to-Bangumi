ALTER TABLE mikan_subscription ADD COLUMN preferred_subtitle TEXT NOT NULL DEFAULT '';
ALTER TABLE mikan_download ADD COLUMN subtitle_scripts INTEGER NOT NULL DEFAULT 0;
UPDATE mikan_download SET identity=json_set(identity,'$[0]',mikan_fold(json_extract(identity,'$[0]')));
CREATE TEMP TABLE mikan_folded_alias AS
 SELECT DISTINCT endpoint,feed,mikan_fold(alias) AS alias,mikan_fold(canonical) AS canonical FROM mikan_title_alias;
DELETE FROM mikan_title_alias;
INSERT INTO mikan_title_alias SELECT * FROM mikan_folded_alias;
DROP TABLE mikan_folded_alias;
