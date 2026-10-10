-- Boss-named monster encounter clone max (1–3). Default 3 = legacy random 1–3.
-- Admin 怪物管理 → 全服怪物百分比. Service also ALTER-adds on first load.

ALTER TABLE `server_monster_global_config`
  ADD COLUMN `boss_encounter_enemy_max` TINYINT UNSIGNED NOT NULL DEFAULT 3
  AFTER `hp_extra_x3_when_mult_ge6`;
