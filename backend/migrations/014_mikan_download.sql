CREATE TABLE mikan_subscription (
 endpoint TEXT NOT NULL, feed TEXT NOT NULL, preferred_group TEXT NOT NULL, preferred_resolution TEXT NOT NULL,
 PRIMARY KEY(endpoint,feed)
);
CREATE TABLE mikan_download (
 endpoint TEXT NOT NULL, feed TEXT NOT NULL, identity TEXT NOT NULL, url TEXT NOT NULL,
 group_name TEXT NOT NULL, resolution TEXT NOT NULL, revision INTEGER NOT NULL,
 PRIMARY KEY(endpoint,feed,url)
);
CREATE INDEX mikan_download_identity ON mikan_download(endpoint,identity);
CREATE TABLE mikan_pending (
 endpoint TEXT NOT NULL, feed TEXT NOT NULL, identity TEXT NOT NULL, url TEXT NOT NULL, title TEXT NOT NULL,
 group_name TEXT NOT NULL, resolution TEXT NOT NULL, revision INTEGER NOT NULL, retry_after INTEGER NOT NULL,
 PRIMARY KEY(endpoint,identity), UNIQUE(endpoint,url)
);
CREATE TABLE mikan_title_alias (
 endpoint TEXT NOT NULL, feed TEXT NOT NULL, alias TEXT NOT NULL, canonical TEXT NOT NULL,
 PRIMARY KEY(endpoint,feed,alias,canonical)
);
