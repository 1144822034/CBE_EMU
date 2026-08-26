USE `jh_online`;

-- Persistent source-IP login lockout shared by game clients, the player
-- account center, and the web admin login.  The service creates this table
-- defensively as well, but applying this migration keeps deployment explicit.
CREATE TABLE IF NOT EXISTS `server_login_ip_blocks` (
  `ip_address` VARCHAR(45) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `ingress_violations` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `block_reason` VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `blocked_at` TIMESTAMP NULL DEFAULT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`ip_address`),
  KEY `idx_login_ip_blocks_blocked` (`blocked`)
) ENGINE=InnoDB;

-- Existing deployments predate ingress-abuse escalation.  Use MySQL
-- 5.x-compatible conditional ALTER statements so the migration is idempotent.
SET @login_ip_ingress_column_sql = IF(
  (SELECT COUNT(*) FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'server_login_ip_blocks'
     AND COLUMN_NAME = 'ingress_violations') = 0,
  'ALTER TABLE `server_login_ip_blocks` ADD COLUMN `ingress_violations` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `failed_attempts`',
  'SELECT 1'
);
PREPARE login_ip_ingress_column_stmt FROM @login_ip_ingress_column_sql;
EXECUTE login_ip_ingress_column_stmt;
DEALLOCATE PREPARE login_ip_ingress_column_stmt;

SET @login_ip_reason_column_sql = IF(
  (SELECT COUNT(*) FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'server_login_ip_blocks'
     AND COLUMN_NAME = 'block_reason') = 0,
  'ALTER TABLE `server_login_ip_blocks` ADD COLUMN `block_reason` VARCHAR(31) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '''' AFTER `blocked`',
  'SELECT 1'
);
PREPARE login_ip_reason_column_stmt FROM @login_ip_reason_column_sql;
EXECUTE login_ip_reason_column_stmt;
DEALLOCATE PREPARE login_ip_reason_column_stmt;
