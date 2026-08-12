USE `jh_online`;

/* Pixi 恩怨：切磋/决斗击杀累计。killer→victim 一行，kill_count 递增。 */
CREATE TABLE IF NOT EXISTS `enmity_kills` (
  `killer_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `killer_role_id` INT UNSIGNED NOT NULL,
  `killer_role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `victim_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `victim_role_id` INT UNSIGNED NOT NULL,
  `victim_role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `kill_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`killer_account_id`, `killer_role_id`, `victim_account_id`, `victim_role_id`),
  KEY `idx_enmity_victim` (`victim_account_id`, `victim_role_id`),
  KEY `idx_enmity_killer` (`killer_account_id`, `killer_role_id`)
) ENGINE=InnoDB;
