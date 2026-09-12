USE `jh_online`;

-- Physical gateway zones for Pixi / host-layer multi-zone selection.
-- Distinct from server_login_servers (title WT 1/1/12 display rows).
CREATE TABLE IF NOT EXISTS `server_gateway_zones` (
  `zone_id` INT UNSIGNED NOT NULL,
  `display_name` VARBINARY(63) NOT NULL,
  `status_label` VARBINARY(63) NOT NULL,
  `ws_url` VARBINARY(191) NOT NULL,
  `account_web_url` VARBINARY(191) NOT NULL,
  `sort_order` INT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `is_default` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`zone_id`),
  KEY `idx_server_gateway_zones_visible` (`enabled`, `sort_order`, `zone_id`)
) ENGINE=InnoDB;

-- UTF-8: 江湖归来 / 推荐 / ws://192.1.1.1:19092. Empty-install seed only.
INSERT IGNORE INTO `server_gateway_zones`
  (`zone_id`, `display_name`, `status_label`, `ws_url`, `account_web_url`,
   `sort_order`, `enabled`, `is_default`)
VALUES
  (1,
   X'E6B19FE6B996E5BD92E69DA5',
   X'E68EA8E88D90',
   X'77733A2F2F3139322E312E312E313A3139303932',
   X'',
   0, 1, 1);
