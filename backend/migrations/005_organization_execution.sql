CREATE TABLE organize_job_005 (
  id INTEGER PRIMARY KEY,
  plan_id INTEGER NOT NULL REFERENCES organize_plan(id),
  status TEXT NOT NULL,
  result_path TEXT,
  failure_details TEXT,
  idempotency_key TEXT NOT NULL UNIQUE,
  result_bytes INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
INSERT INTO organize_job_005(id,plan_id,status,result_path,failure_details,idempotency_key,created_at,updated_at)
  SELECT id,plan_id,status,result_path,failure_details,'legacy:' || id,created_at,updated_at
  FROM organize_job;
DROP TABLE organize_job;
ALTER TABLE organize_job_005 RENAME TO organize_job;
CREATE INDEX organize_job_plan_id ON organize_job(plan_id);
