USE `jh_online`;

/* Ranking board type=2 (PK榜) sorts/displays pk_points instead of money.
 * Safe to re-run: skips ADD COLUMN when the column already exists. */

SET @pk_points_exists := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'account_roles'
    AND COLUMN_NAME = 'pk_points'
);

SET @pk_points_sql := IF(
  @pk_points_exists = 0,
  'ALTER TABLE `account_roles` ADD COLUMN `pk_points` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `wcoin`',
  'SELECT 1'
);

PREPARE pk_points_stmt FROM @pk_points_sql;
EXECUTE pk_points_stmt;
DEALLOCATE PREPARE pk_points_stmt;
