CREATE TABLE mikan_rule_history (
 endpoint TEXT NOT NULL, feed TEXT NOT NULL, episode TEXT NOT NULL, identity TEXT NOT NULL DEFAULT '',
 PRIMARY KEY(endpoint,feed,episode,identity)
);
CREATE TABLE mikan_rule_baseline (
 endpoint TEXT NOT NULL, feed TEXT NOT NULL, url TEXT NOT NULL, PRIMARY KEY(endpoint,feed,url)
);
