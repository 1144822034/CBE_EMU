-- Pixi cosmetics unlock lists + equipped ids (chenghao / wing / mount).
-- Not related to special_designation_mask / arena_designation_mask.

ALTER TABLE account_roles
  ADD COLUMN unlocked_chenghao_ids TEXT NULL,
  ADD COLUMN unlocked_wing_ids TEXT NULL,
  ADD COLUMN unlocked_mount_ids TEXT NULL,
  ADD COLUMN equipped_chenghao_id INT UNSIGNED NOT NULL DEFAULT 0,
  ADD COLUMN equipped_wing_id INT UNSIGNED NOT NULL DEFAULT 0,
  ADD COLUMN equipped_mount_id INT UNSIGNED NOT NULL DEFAULT 0;
