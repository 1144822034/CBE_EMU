USE `jh_online`;

-- Backend-login-only source-IP risk policy.  The administrator listener
-- counts failed credentials separately so its five-attempt lockout never
-- changes the game's or player account centre's shared 15-attempt policy.
CREATE TABLE IF NOT EXISTS `server_admin_login_ip_blocks` (
  `ip_address` VARCHAR(15) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `last_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked_at` TIMESTAMP NULL DEFAULT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`ip_address`),
  KEY `idx_admin_login_ip_blocks_blocked` (`blocked`)
) ENGINE=InnoDB;
