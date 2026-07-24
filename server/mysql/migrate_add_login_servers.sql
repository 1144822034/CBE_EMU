USE `jh_online`;

CREATE TABLE IF NOT EXISTS `server_login_servers` (
  `server_id` INT UNSIGNED NOT NULL,
  `display_name` VARBINARY(31) NOT NULL,
  `status_label` VARBINARY(31) NOT NULL,
  `display_color` MEDIUMINT UNSIGNED NOT NULL DEFAULT 16777215,
  `sort_order` INT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`server_id`),
  KEY `idx_server_login_servers_visible` (`enabled`, `sort_order`, `server_id`)
) ENGINE=InnoDB;

-- GBK: 江湖一区 / 推荐. It only seeds an empty installation.
INSERT IGNORE INTO `server_login_servers`
  (`server_id`, `display_name`, `status_label`, `display_color`, `sort_order`, `enabled`)
VALUES
  (1, X'BDADBAFED2BBC7F8', X'CDC6BCF6', 16777215, 0, 1);
