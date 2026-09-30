CREATE TABLE novel_work (
 id INTEGER PRIMARY KEY,
 import_key TEXT NOT NULL UNIQUE,
 title TEXT NOT NULL,
 author TEXT NOT NULL DEFAULT '',
 summary TEXT NOT NULL DEFAULT '',
 subject_id INTEGER,
 manual_metadata INTEGER NOT NULL DEFAULT 0,
 cover BLOB,
 cover_mime TEXT NOT NULL DEFAULT ''
);
CREATE TABLE novel_source (
 id INTEGER PRIMARY KEY,
 path TEXT NOT NULL UNIQUE,
 is_directory INTEGER NOT NULL
);
CREATE TABLE novel_file (
 id INTEGER PRIMARY KEY,
 work_id INTEGER NOT NULL REFERENCES novel_work(id),
 source_id INTEGER NOT NULL REFERENCES novel_source(id),
 path TEXT NOT NULL UNIQUE,
 label TEXT NOT NULL,
 missing INTEGER NOT NULL DEFAULT 0,
 subject_id INTEGER,
 cover BLOB,
 cover_mime TEXT NOT NULL DEFAULT ''
);
CREATE INDEX novel_file_work ON novel_file(work_id);
