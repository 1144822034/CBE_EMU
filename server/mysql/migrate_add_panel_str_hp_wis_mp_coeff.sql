-- Panel: strength → HP / wisdom → MP coefficients (×100; 50 = 0.5).
-- Safe to re-run: skips ADD COLUMN when the column already exists.
-- Runtime also ensures via mock_server_combat_formula_config.inc.c.

SET @col := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'server_combat_formula_config'
    AND COLUMN_NAME = 'panel_strength_hp_coeff_x100'
);
SET @sql := IF(
  @col = 0,
  'ALTER TABLE `server_combat_formula_config` ADD COLUMN `panel_strength_hp_coeff_x100` INT UNSIGNED NOT NULL DEFAULT 50 AFTER `panel_hit_base`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @col := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE()
    AND TABLE_NAME = 'server_combat_formula_config'
    AND COLUMN_NAME = 'panel_wisdom_mp_coeff_x100'
);
SET @sql := IF(
  @col = 0,
  'ALTER TABLE `server_combat_formula_config` ADD COLUMN `panel_wisdom_mp_coeff_x100` INT UNSIGNED NOT NULL DEFAULT 50 AFTER `panel_strength_hp_coeff_x100`',
  'SELECT 1'
);
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
