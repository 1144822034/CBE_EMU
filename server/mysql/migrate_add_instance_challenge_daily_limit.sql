-- Add per-scene daily challenge quota for instance-guide「挑战守关怪」.
-- Safe to re-run: skips ADD COLUMN when the column already exists.
-- Runtime also ALTER on ensure_tables.

SET @col := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'server_instance_enter_rules'
    AND COLUMN_NAME = 'challenge_daily_limit'
);
SET @sql := IF(
  @col = 0,
  'ALTER TABLE `server_instance_enter_rules` ADD COLUMN `challenge_daily_limit` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `daily_limit`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'server_instance_enter_daily'
    AND COLUMN_NAME = 'challenge_count'
);
SET @sql := IF(
  @col = 0,
  'ALTER TABLE `server_instance_enter_daily` ADD COLUMN `challenge_count` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `enter_count`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
