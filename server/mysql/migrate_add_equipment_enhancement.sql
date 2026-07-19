USE `jh_online`;

ALTER TABLE `account_role_backpack`
  ADD COLUMN `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0
  AFTER `item_count`;
