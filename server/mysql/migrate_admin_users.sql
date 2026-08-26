USE `jh_online`;

-- Provision independent operator accounts.  This table is intentionally
-- separate from player `accounts`; no default administrator is created.
CREATE TABLE IF NOT EXISTS `server_admin_users` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `password_value` VARBINARY(64) NOT NULL,
  `failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `locked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_admin_operation_logs` (
  `log_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `operator_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `action_code` VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `target_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `target_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `change_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `detail` VARBINARY(255) NOT NULL,
  `created_at` TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`log_id`),
  KEY `idx_admin_operation_logs_account` (`target_account_id`, `log_id`),
  KEY `idx_admin_operation_logs_created` (`created_at`, `log_id`)
) ENGINE=InnoDB;

-- Add the operator column to an existing audit table.  Empty values are
-- retained for historical rows; new web-admin operations always write the
-- authenticated server_admin_users.account_id.
SET @admin_operator_column_sql = IF(
  (SELECT COUNT(*) FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'server_admin_operation_logs'
     AND COLUMN_NAME = 'operator_account_id') = 0,
  'ALTER TABLE `server_admin_operation_logs` ADD COLUMN `operator_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '''' AFTER `log_id`',
  'SELECT 1'
);
PREPARE admin_operator_column_stmt FROM @admin_operator_column_sql;
EXECUTE admin_operator_column_stmt;
DEALLOCATE PREPARE admin_operator_column_stmt;
