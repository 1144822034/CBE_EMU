-- Chest 玄晶 pity (hard guarantee after N consecutive opens without crystal).
USE `jh_online`;

CREATE TABLE IF NOT EXISTS `server_chest_pity_config` (
  `chest_item_id` INT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `pity_threshold` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '第N次保底；0=关闭',
  `pity_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`chest_item_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Gold chest default: 第99次保底十二级玄晶(912).
INSERT IGNORE INTO `server_chest_pity_config`
  (`chest_item_id`, `enabled`, `pity_threshold`, `pity_item_id`)
VALUES (524, 1, 99, 912);

-- Per-role miss counters keyed by chest kind order 522,523,524,532,534.
ALTER TABLE `account_roles`
  ADD COLUMN `chest_crystal_miss` VARCHAR(64) NOT NULL DEFAULT '0,0,0,0,0'
  AFTER `pk_pve_streak`;
