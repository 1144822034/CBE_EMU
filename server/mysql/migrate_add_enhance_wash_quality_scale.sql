-- Wash pool quality-tier scaling + first-obtain seed percent (admin; defaults ON/20).
CREATE TABLE IF NOT EXISTS `server_enhance_wash_settings` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `quality_scale_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT '1=value_max*(Q+1)/5; 0=full value_max',
  `first_seed_percent` TINYINT UNSIGNED NOT NULL DEFAULT 20
    COMMENT 'first-obtain wash pool roll chance 0..100',
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `server_enhance_wash_settings`
  (`id`, `quality_scale_enabled`, `first_seed_percent`)
VALUES (1, 1, 20);

-- Existing installs that already have the table without first_seed_percent:
-- ALTER TABLE `server_enhance_wash_settings`
--   ADD COLUMN `first_seed_percent` TINYINT UNSIGNED NOT NULL DEFAULT 20
--   AFTER `quality_scale_enabled`;
-- (runtime also probes information_schema and ALTERs if missing)
