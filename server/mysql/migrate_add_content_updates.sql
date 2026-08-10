-- Startup game-data content update manifest (all non-CBM resource leaves).
--
-- The names are stored as raw protocol bytes because Jianghu OL resource names
-- may be GBK.  A single config row maps directly to the client WT 18/9 and
-- WT 18/8 id/code pair; ordered child rows map to the exact WT 18/8 payload.

CREATE TABLE IF NOT EXISTS `server_content_update_releases` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `release_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `manifest_code` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_content_update_files` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `sort_order` SMALLINT UNSIGNED NOT NULL,
  `resource_name` VARBINARY(127) NOT NULL,
  PRIMARY KEY (`config_id`, `sort_order`),
  UNIQUE KEY `uq_content_update_resource` (`config_id`, `resource_name`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_content_update_releases`
  (`config_id`, `enabled`, `release_id`, `manifest_code`)
VALUES (1, 0, 0, 0);
