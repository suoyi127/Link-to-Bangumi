ALTER TABLE game_resource ADD COLUMN vndb_id TEXT NOT NULL DEFAULT '';
ALTER TABLE game_resource ADD COLUMN metadata_source TEXT NOT NULL DEFAULT '';
UPDATE game_resource SET metadata_source='bangumi' WHERE subject_id IS NOT NULL;
