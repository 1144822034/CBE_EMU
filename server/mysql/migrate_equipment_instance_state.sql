USE `jh_online`;

-- Equipment is a concrete item instance.  Its enhancement and current/max
-- durability must survive equipment <-> backpack moves, trades and relogin.
-- Run once on an existing installation before upgrading the mock service.
-- The service also verifies these columns at startup for older deployments.

ALTER TABLE `account_role_equipment`
  ADD COLUMN `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `item_id`,
  ADD COLUMN `enhance_affix_types` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `enhance_level`,
  ADD COLUMN `enhance_affix_values` BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER `enhance_affix_types`,
  ADD COLUMN `durability` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `enhance_level`,
  ADD COLUMN `durability_max` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `durability`;

ALTER TABLE `account_role_backpack`
  ADD COLUMN `durability` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `enhance_level`,
  ADD COLUMN `durability_max` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `durability`,
  ADD COLUMN `enhance_affix_types` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `enhance_level`,
  ADD COLUMN `enhance_affix_values` BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER `enhance_affix_types`;

-- Existing worn durability is intentionally not copied with a blind SQL
-- update: the server joins it only when the historical item_id matches the
-- currently equipped item, validates the equip.dsh maximum, then rewrites the
-- role atomically as format_version=6 on the next login.
