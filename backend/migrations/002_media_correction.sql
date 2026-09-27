ALTER TABLE media_file ADD COLUMN title TEXT;
ALTER TABLE media_file ADD COLUMN season TEXT;
CREATE UNIQUE INDEX anime_bangumi_subject_unique ON anime(bangumi_subject_id);
