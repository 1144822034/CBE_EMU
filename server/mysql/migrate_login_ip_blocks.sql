USE `jh_online`;

-- Persistent source-IP login lockout shared by game clients, the player
-- account center, and the web admin login.  The service creates this table
-- defensively as well, but applying this migration keeps deployment explicit.
CREATE TABLE IF NOT EXISTS `server_login_ip_blocks` (
  `ip_address` VARCHAR(45) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked_at` TIMESTAMP NULL DEFAULT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`ip_address`),
  KEY `idx_login_ip_blocks_blocked` (`blocked`)
) ENGINE=InnoDB;
