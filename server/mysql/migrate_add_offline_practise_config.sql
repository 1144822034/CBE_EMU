-- Offline practise (修炼丹 827) settle multiplier:
--   exp = settle_minutes * level^2 * exp_level_minute_mult
-- Admin adjustable; default 8.
CREATE TABLE IF NOT EXISTS `server_offline_practise_config` (
  `id` TINYINT UNSIGNED NOT NULL,
  `exp_level_minute_mult` INT UNSIGNED NOT NULL DEFAULT 8
    COMMENT 'exp = settle_minutes * level^2 * mult',
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_offline_practise_config` (`id`, `exp_level_minute_mult`)
VALUES (1, 8);
