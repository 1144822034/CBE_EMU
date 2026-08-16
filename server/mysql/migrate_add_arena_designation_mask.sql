-- Pixi arena kill-streak text titles (designation id 30-40).
-- Runtime also ensures this column via mock_server_role.c.
ALTER TABLE account_roles
  ADD COLUMN arena_designation_mask INT UNSIGNED NOT NULL DEFAULT 0
  AFTER designation_id;
