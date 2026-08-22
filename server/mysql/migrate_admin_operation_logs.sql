-- Persistent audit trail for successful web-administration account/role actions
-- and committed in-game W-coin spending. It intentionally has no foreign keys:
-- an audit record must survive later role migration or deletion.
CREATE TABLE IF NOT EXISTS `server_admin_operation_logs` (
  `log_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
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
