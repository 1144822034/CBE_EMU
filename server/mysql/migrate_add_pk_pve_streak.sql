-- Field PK decay streak (wins toward next -1 pk_points).
-- Prefer runtime ensure via mock_role_ensure_pk_columns; this is for manual ops.
ALTER TABLE account_roles
  ADD COLUMN pk_pve_streak TINYINT UNSIGNED NOT NULL DEFAULT 0
  AFTER pk_points;
