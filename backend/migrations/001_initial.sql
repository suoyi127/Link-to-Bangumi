CREATE TABLE anime (
  id INTEGER PRIMARY KEY, display_title TEXT NOT NULL, original_title TEXT,
  season TEXT, year INTEGER, bangumi_subject_id INTEGER, cover_url TEXT,
  locked INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE anime_alias (
  id INTEGER PRIMARY KEY, anime_id INTEGER NOT NULL REFERENCES anime(id),
  normalized_alias TEXT NOT NULL, source TEXT NOT NULL,
  UNIQUE(anime_id, normalized_alias)
);
CREATE INDEX anime_alias_lookup ON anime_alias(normalized_alias);
CREATE TABLE scan_job (
  id INTEGER PRIMARY KEY, source TEXT NOT NULL, status TEXT NOT NULL,
  discovered_count INTEGER NOT NULL DEFAULT 0, processed_count INTEGER NOT NULL DEFAULT 0,
  error_count INTEGER NOT NULL DEFAULT 0, error_summary TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE media_file (
  id INTEGER PRIMARY KEY, scan_id INTEGER REFERENCES scan_job(id),
  anime_id INTEGER REFERENCES anime(id), source_path TEXT NOT NULL UNIQUE,
  library_path TEXT UNIQUE, filename TEXT NOT NULL, episode_number TEXT,
  episode_type TEXT NOT NULL DEFAULT 'normal', size_bytes INTEGER NOT NULL,
  link_mode TEXT, confidence REAL NOT NULL DEFAULT 0, status TEXT NOT NULL,
  torrent_hash TEXT, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX media_file_status ON media_file(status);
CREATE TABLE organize_plan (
  id INTEGER PRIMARY KEY, media_file_id INTEGER NOT NULL REFERENCES media_file(id),
  source_path TEXT NOT NULL, source_size INTEGER NOT NULL, source_modified_at TEXT NOT NULL,
  target_path TEXT NOT NULL, operation TEXT NOT NULL, expires_at TEXT NOT NULL,
  execution_state TEXT NOT NULL DEFAULT 'pending', idempotency_key TEXT NOT NULL UNIQUE,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE organize_job (
  id INTEGER PRIMARY KEY, plan_id INTEGER NOT NULL REFERENCES organize_plan(id),
  status TEXT NOT NULL, result_path TEXT, failure_details TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE bangumi_cache (
  query_key TEXT PRIMARY KEY, response_json TEXT NOT NULL, expires_at TEXT NOT NULL,
  retry_count INTEGER NOT NULL DEFAULT 0, retry_after TEXT,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE setting (key TEXT PRIMARY KEY, value_json TEXT NOT NULL,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
CREATE TABLE audit_log (
  id INTEGER PRIMARY KEY, action TEXT NOT NULL, entity_type TEXT NOT NULL,
  entity_id TEXT NOT NULL, details_json TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
