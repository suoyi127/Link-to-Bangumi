CREATE TABLE IF NOT EXISTS watch_progress (
  media_id INTEGER PRIMARY KEY REFERENCES media_file(id) ON DELETE CASCADE,
  session_id INTEGER NOT NULL DEFAULT 0,
  position REAL NOT NULL DEFAULT 0, duration REAL NOT NULL DEFAULT 0,
  watched INTEGER NOT NULL DEFAULT 0 CHECK(watched IN (0,1)),
  played_ranges TEXT NOT NULL DEFAULT '[]', covered REAL NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL DEFAULT 0, activity_id INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS watch_clock (singleton INTEGER PRIMARY KEY CHECK(singleton=1), value INTEGER NOT NULL);
INSERT OR IGNORE INTO watch_clock VALUES(1,0);

-- 用当前媒体归属计算分集，同一文件即使经过整理、校正、合并仍保留自己的时间点。
CREATE VIEW IF NOT EXISTS watch_episode AS
WITH numbered AS (
  SELECT m.*, CASE
    WHEN episode_number<>'' AND episode_number NOT GLOB '*[^0-9]*'
      THEN CAST(CAST(episode_number AS INTEGER) AS TEXT)
    WHEN length(episode_number)>2 AND episode_number GLOB '*.[0-9]'
      AND substr(episode_number,1,length(episode_number)-2) NOT GLOB '*[^0-9]*'
      THEN CAST(CAST(episode_number AS INTEGER) AS TEXT) ||
        CASE WHEN substr(episode_number,-1)='0' THEN '' ELSE substr(episode_number,-2) END
  END AS episode_no FROM media_file m
)
SELECT m.id AS media_id,m.anime_id,a.display_title AS title,m.episode_type,m.episode_number,m.episode_no,m.status,
       COALESCE(NULLIF(m.season,''),a.season,'') AS season,
       json_array(m.anime_id,COALESCE(NULLIF(m.season,''),a.season,''),m.episode_type,m.episode_no,
         CASE WHEN m.anime_id IS NULL OR m.episode_no IS NULL OR m.episode_type='unknown' THEN m.id END) AS episode_key
FROM numbered m LEFT JOIN anime a ON a.id=m.anime_id;
CREATE VIEW IF NOT EXISTS watch_episode_state AS
SELECT e.*,COALESCE(p.position,0) AS position,COALESCE(p.duration,0) AS duration,
       COALESCE(p.covered,0) AS covered,COALESCE(p.updated_at,0) AS updated_at,COALESCE(p.activity_id,0) AS activity_id,
       MAX(COALESCE(p.watched,0)) OVER (PARTITION BY e.episode_key) AS watched
FROM watch_episode e LEFT JOIN watch_progress p ON p.media_id=e.media_id;

CREATE TABLE watch_sync_account (
    singleton INTEGER PRIMARY KEY CHECK(singleton=1), account INTEGER NOT NULL DEFAULT 0,
    username TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 0,
    connected INTEGER NOT NULL DEFAULT 0, error TEXT NOT NULL DEFAULT ''
);
INSERT INTO watch_sync_account(singleton) VALUES(1);
CREATE TABLE watch_sync_queue (
    id INTEGER PRIMARY KEY AUTOINCREMENT, account INTEGER NOT NULL,
    media_id INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
    attempts INTEGER NOT NULL DEFAULT 0, retry_at INTEGER NOT NULL DEFAULT 0,
    error TEXT NOT NULL DEFAULT '', UNIQUE(account,media_id)
);
CREATE INDEX watch_sync_due ON watch_sync_queue(account,retry_at,id);

CREATE TRIGGER watch_sync_insert AFTER INSERT ON watch_progress WHEN NEW.watched=1 BEGIN
    INSERT OR IGNORE INTO watch_sync_queue(account,media_id)
    SELECT account,NEW.media_id FROM watch_sync_account WHERE enabled=1 AND account>0;
END;
CREATE TRIGGER watch_sync_complete AFTER UPDATE OF watched ON watch_progress WHEN NEW.watched=1 AND OLD.watched=0 BEGIN
    INSERT OR IGNORE INTO watch_sync_queue(account,media_id)
    SELECT account,NEW.media_id FROM watch_sync_account WHERE enabled=1 AND account>0;
END;
CREATE TRIGGER watch_sync_cancel AFTER UPDATE OF watched ON watch_progress WHEN NEW.watched=0 AND OLD.watched=1 BEGIN
    DELETE FROM watch_sync_queue WHERE media_id=NEW.media_id;
END;
CREATE TRIGGER watch_sync_remove AFTER DELETE ON watch_progress BEGIN
    DELETE FROM watch_sync_queue WHERE media_id=OLD.media_id;
END;

ALTER TABLE watch_sync_queue ADD COLUMN blocked_subject INTEGER;
CREATE TABLE watch_bangumi_episode (
    media_id INTEGER PRIMARY KEY REFERENCES media_file(id) ON DELETE CASCADE,
    subject INTEGER NOT NULL, episode_type TEXT NOT NULL, number TEXT NOT NULL,
    raw_number TEXT NOT NULL, source TEXT NOT NULL, episode_id INTEGER NOT NULL
);
