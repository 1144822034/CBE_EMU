-- Persistent audit trail for successful web-administration account/role actions
-- and committed in-game W-coin spending. It intentionally has no foreign keys:
-- an audit record must survive later role migration or deletion.
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

-- Existing tables predate per-operator auditing.  Use a MySQL 5.x-compatible
-- conditional ALTER so this script remains idempotent.
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
