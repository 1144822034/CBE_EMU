-- Special permanent titles (designation id 41-45).
-- Runtime also ensures this column via mock_server_role.c.
ALTER TABLE account_roles
  ADD COLUMN special_designation_mask INT UNSIGNED NOT NULL DEFAULT 0
  AFTER arena_designation_mask;
