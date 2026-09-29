CREATE TABLE folder_import (
  id INTEGER PRIMARY KEY,
  root_path TEXT NOT NULL UNIQUE,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
ALTER TABLE media_file ADD COLUMN folder_import_id INTEGER REFERENCES folder_import(id);
CREATE INDEX media_file_folder_import ON media_file(folder_import_id);
