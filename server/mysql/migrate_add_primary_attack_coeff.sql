-- Per-job primary-attribute → physical attack coefficients (admin combat formula).
-- Safe to re-run: skips ADD COLUMN when the column already exists.
-- Runtime also ensures via mock_server_combat_formula_config.inc.c.

SET @col := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'server_combat_formula_config'
    AND COLUMN_NAME = 'primary_attack_coeff_tianji_percent'
);
SET @sql := IF(
  @col = 0,
  'ALTER TABLE `server_combat_formula_config` ADD COLUMN `primary_attack_coeff_tianji_percent` INT UNSIGNED NOT NULL DEFAULT 100 AFTER `panel_hit_base`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'server_combat_formula_config'
    AND COLUMN_NAME = 'primary_attack_coeff_huanjian_percent'
);
SET @sql := IF(
  @col = 0,
  'ALTER TABLE `server_combat_formula_config` ADD COLUMN `primary_attack_coeff_huanjian_percent` INT UNSIGNED NOT NULL DEFAULT 100 AFTER `primary_attack_coeff_tianji_percent`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'server_combat_formula_config'
    AND COLUMN_NAME = 'primary_attack_coeff_guidao_percent'
);
SET @sql := IF(
  @col = 0,
  'ALTER TABLE `server_combat_formula_config` ADD COLUMN `primary_attack_coeff_guidao_percent` INT UNSIGNED NOT NULL DEFAULT 100 AFTER `primary_attack_coeff_huanjian_percent`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
