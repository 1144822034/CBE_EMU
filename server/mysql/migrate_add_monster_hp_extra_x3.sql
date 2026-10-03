-- Optional extra HP ×3 when level combat multiplier >= 6 (Lv>=56).
-- Default 0 = off (matches 2026-10-03). Admin 怪物管理 checkbox.
-- Service also ALTER-adds this column on first load.

ALTER TABLE `server_monster_global_config`
  ADD COLUMN `hp_extra_x3_when_mult_ge6` TINYINT UNSIGNED NOT NULL DEFAULT 0
  AFTER `reward_money_percent`;
