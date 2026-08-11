-- Enhance milestone defaults + washable attr pool + per-item wash overrides.
USE `jh_online`;

CREATE TABLE IF NOT EXISTS `server_enhance_milestones` (
  `equip_slot` TINYINT UNSIGNED NOT NULL COMMENT '0武器..7饰品',
  `unlock_level` TINYINT UNSIGNED NOT NULL COMMENT '4/8/12/16',
  `attr_type` TINYINT UNSIGNED NOT NULL COMMENT '1力..10法力',
  `flag` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0值 1%',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`equip_slot`, `unlock_level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `server_enhance_attr_pool` (
  `attr_type` TINYINT UNSIGNED NOT NULL,
  `flag` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `value_min` INT UNSIGNED NOT NULL DEFAULT 1,
  `value_max` INT UNSIGNED NOT NULL DEFAULT 1,
  `weight` INT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`attr_type`, `flag`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Compact text: w,t,f,v,c;... for +4/+8/+12/+16
-- Skip if column already exists on upgraded DBs.
ALTER TABLE `account_role_backpack`
  ADD COLUMN `enhance_wash` VARCHAR(96) NOT NULL DEFAULT
    '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0'
  AFTER `enhance_level`;

ALTER TABLE `account_role_equipment`
  ADD COLUMN `enhance_wash` VARCHAR(96) NOT NULL DEFAULT
    '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0'
  AFTER `enhance_level`;

ALTER TABLE `account_role_warehouse`
  ADD COLUMN `enhance_wash` VARCHAR(96) NOT NULL DEFAULT
    '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0'
  AFTER `enhance_level`;

ALTER TABLE `server_auction_listings`
  ADD COLUMN `enhance_wash` VARCHAR(96) NOT NULL DEFAULT
    '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0'
  AFTER `enhance_level`;

ALTER TABLE `server_auction_claims`
  ADD COLUMN `enhance_wash` VARCHAR(96) NOT NULL DEFAULT
    '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0'
  AFTER `enhance_level`;
