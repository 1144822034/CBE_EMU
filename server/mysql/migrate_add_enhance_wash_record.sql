-- Per-role equipment wash snapshot keyed by item_instance_id.
USE `jh_online`;

CREATE TABLE IF NOT EXISTS `account_role_enhance_wash_record` (
  `role_id` INT UNSIGNED NOT NULL,
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `bag_seq` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT
    '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `unlock_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `washed_unix` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`role_id`, `bag_seq`),
  KEY `idx_role_item_instance` (`role_id`, `item_instance_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
