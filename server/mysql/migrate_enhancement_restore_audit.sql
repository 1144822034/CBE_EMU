-- Add the audit table used by the restricted historical enhancement restore.
-- This migration is idempotent and does not touch equipment data.
CREATE TABLE IF NOT EXISTS `server_admin_enhancement_restore_audit` (
  `audit_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `source_name` VARCHAR(127) NOT NULL,
  `source_row_count` INT UNSIGNED NOT NULL,
  `matched_row_count` INT UNSIGNED NOT NULL,
  `changed_row_count` INT UNSIGNED NOT NULL,
  `missing_row_count` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`audit_id`)
) ENGINE=InnoDB;
