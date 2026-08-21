USE `jh_online`;

-- Equipment is a concrete item instance.  Its enhancement and current/max
-- durability must survive equipment <-> backpack moves, trades and relogin.
-- Run once on an existing installation before upgrading the mock service.
-- The service also verifies these columns at startup for older deployments.

-- A populated table without enhance_level has no instance-safe source from
-- which SQL can reconstruct worn equipment levels.  Abort before ALTER TABLE
-- can initialize every row to DEFAULT 0.  Empty/new installations remain
-- eligible for the normal schema upgrade below.
DROP PROCEDURE IF EXISTS `cbe_guard_equipment_enhance_level_migration`;

DELIMITER //
CREATE PROCEDURE `cbe_guard_equipment_enhance_level_migration`()
BEGIN
  DECLARE equipment_has_level INT UNSIGNED DEFAULT 0;
  DECLARE equipment_rows BIGINT UNSIGNED DEFAULT 0;
  DECLARE backpack_has_level INT UNSIGNED DEFAULT 0;
  DECLARE backpack_rows BIGINT UNSIGNED DEFAULT 0;

  SELECT COUNT(*) INTO equipment_has_level
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='account_role_equipment'
    AND COLUMN_NAME='enhance_level';
  SELECT COUNT(*) INTO equipment_rows FROM account_role_equipment;
  SELECT COUNT(*) INTO backpack_has_level
  FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='account_role_backpack'
    AND COLUMN_NAME='enhance_level';
  SELECT COUNT(*) INTO backpack_rows FROM account_role_backpack;

  IF equipment_has_level=0 AND equipment_rows<>0 THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT='refusing to add account_role_equipment.enhance_level to a populated table; back up and run an instance-aware migration';
  END IF;
  IF backpack_has_level=0 AND backpack_rows<>0 THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT='refusing to add account_role_backpack.enhance_level to a populated table; back up and run an instance-aware migration';
  END IF;
END//
DELIMITER ;

CALL `cbe_guard_equipment_enhance_level_migration`();
DROP PROCEDURE `cbe_guard_equipment_enhance_level_migration`;

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

-- Do not infer an equipped instance's level from account/role/item_id.  The
-- old schema did not persist the backpack sequence in the equipment slot, so
-- multiple equal item ids are not provably the same instance.  Stop the
-- service, take a backup, and perform an instance-aware migration before
-- adding this column to a non-empty legacy equipment table.

-- Existing worn durability is intentionally not copied with a blind SQL
-- update: the server joins it only when the historical item_id matches the
-- currently equipped item, validates the equip.dsh maximum, then rewrites the
-- role atomically as format_version=6 on the next login.
