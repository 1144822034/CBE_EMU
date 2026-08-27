USE `jh_online`;

/* Friend intimacy timestamps + role spouse columns.
 * Safe to re-run: skips ADD COLUMN when already present. */

SET @col_exists := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'friendships'
    AND COLUMN_NAME = 'last_team_at'
);
SET @sql := IF(
  @col_exists = 0,
  'ALTER TABLE `friendships` ADD COLUMN `last_team_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP AFTER `target_sex`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col_exists := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'friendships'
    AND COLUMN_NAME = 'last_decay_at'
);
SET @sql := IF(
  @col_exists = 0,
  'ALTER TABLE `friendships` ADD COLUMN `last_decay_at` TIMESTAMP NULL DEFAULT NULL AFTER `last_team_at`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col_exists := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'account_roles'
    AND COLUMN_NAME = 'spouse_role_id'
);
SET @sql := IF(
  @col_exists = 0,
  'ALTER TABLE `account_roles` ADD COLUMN `spouse_role_id` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `equipped_mount_id`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col_exists := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'account_roles'
    AND COLUMN_NAME = 'spouse_account_id'
);
SET @sql := IF(
  @col_exists = 0,
  'ALTER TABLE `account_roles` ADD COLUMN `spouse_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT \'\' AFTER `spouse_role_id`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col_exists := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'account_roles'
    AND COLUMN_NAME = 'spouse_name'
);
SET @sql := IF(
  @col_exists = 0,
  'ALTER TABLE `account_roles` ADD COLUMN `spouse_name` VARBINARY(32) NOT NULL DEFAULT \'\' AFTER `spouse_account_id`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
