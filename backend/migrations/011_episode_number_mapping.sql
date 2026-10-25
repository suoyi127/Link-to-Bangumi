ALTER TABLE media_file ADD COLUMN raw_episode_number TEXT NOT NULL DEFAULT '';
ALTER TABLE media_file ADD COLUMN episode_number_source TEXT NOT NULL DEFAULT 'parsed' CHECK(episode_number_source IN ('parsed','bangumi','manual'));
UPDATE media_file SET raw_episode_number=COALESCE(episode_number,'');
-- 旧版本未单独记录修正来源，保守保护有人工整理或集数编辑记录的文件。
UPDATE media_file SET episode_number_source='manual' WHERE
 EXISTS(SELECT 1 FROM audit_log WHERE action='episode_edited' AND entity_type='media_file' AND entity_id=CAST(media_file.id AS TEXT)) OR
 EXISTS(SELECT 1 FROM anime_alias WHERE anime_id=media_file.anime_id AND source='user');
CREATE TABLE bangumi_episode_mapping (
 subject_id INTEGER NOT NULL,
 sort_number TEXT NOT NULL,
 ep_number TEXT NOT NULL,
 PRIMARY KEY(subject_id,sort_number),
 UNIQUE(subject_id,ep_number)
);
