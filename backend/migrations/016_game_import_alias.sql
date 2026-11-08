CREATE TABLE IF NOT EXISTS game_import_alias (
    alias_id INTEGER PRIMARY KEY REFERENCES game_resource(id) ON DELETE CASCADE,
    game_id INTEGER NOT NULL REFERENCES game_resource(id) ON DELETE CASCADE,
    CHECK (alias_id <> game_id)
);
CREATE INDEX IF NOT EXISTS game_import_alias_target ON game_import_alias(game_id);
