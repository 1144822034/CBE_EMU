-- Pixi client UI visibility: wash button + wing/mount status-strip badges.
CREATE TABLE IF NOT EXISTS `server_client_ui_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `wash_button_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT '1=show bag/enhance 洗练 action; 0=hide + reject wash ops',
  `wing_status_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT '1=show map-corner 翅 badge',
  `mount_status_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT '1=show map-corner 骑 badge',
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `server_client_ui_config`
  (`id`, `wash_button_enabled`, `wing_status_enabled`, `mount_status_enabled`)
VALUES (1, 1, 1, 1);
