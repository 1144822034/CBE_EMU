-- Daily self-registration limit (admin adjustable; default 3).
CREATE TABLE IF NOT EXISTS `server_registration_config` (
  `id` TINYINT UNSIGNED NOT NULL,
  `daily_limit` INT UNSIGNED NOT NULL DEFAULT 3,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_registration_config` (`id`, `daily_limit`) VALUES (1, 3);
