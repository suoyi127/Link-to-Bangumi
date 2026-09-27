ALTER TABLE media_file ADD COLUMN origin TEXT NOT NULL DEFAULT 'qb_download'
  CHECK (origin IN ('qb_download', 'external_import'));
