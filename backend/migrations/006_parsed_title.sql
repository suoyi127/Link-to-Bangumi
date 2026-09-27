ALTER TABLE media_file ADD COLUMN parsed_title TEXT NOT NULL DEFAULT '';
UPDATE media_file SET parsed_title=COALESCE(title,'') WHERE parsed_title='';
