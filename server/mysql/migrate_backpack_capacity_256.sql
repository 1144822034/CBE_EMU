USE `jh_online`;

-- Pixi backpack hard cap raised to 256 (CBE 64-slot UI no longer binding).
-- TINYINT UNSIGNED maxes at 255 and truncates capacity/count at the DB edge.
ALTER TABLE `account_roles`
  MODIFY COLUMN `backpack_capacity` SMALLINT UNSIGNED NOT NULL,
  MODIFY COLUMN `backpack_item_count` SMALLINT UNSIGNED NOT NULL;
