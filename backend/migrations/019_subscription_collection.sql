-- 仅为新订阅记录当时授权的账号。完成标志使多字幕组订阅和重试不会重复收藏。
CREATE TABLE subscription_collection (
    account INTEGER NOT NULL, mikan_id INTEGER NOT NULL, endpoint TEXT NOT NULL, feed TEXT NOT NULL,
    ready INTEGER NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0,
    attempts INTEGER NOT NULL DEFAULT 0, retry_at INTEGER NOT NULL DEFAULT 0,
    error TEXT NOT NULL DEFAULT '', PRIMARY KEY(account,mikan_id)
);
CREATE INDEX subscription_collection_due ON subscription_collection(account,ready,completed,retry_at);
CREATE INDEX subscription_collection_activation ON subscription_collection(endpoint,feed) WHERE ready=0;
