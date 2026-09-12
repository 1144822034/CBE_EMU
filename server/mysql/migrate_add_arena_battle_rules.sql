-- Timed-arena duel rule toggles (admin).
-- Defaults preserve historical contract:
--   damage deepen ON, heal OFF, dodge ON, combat pill OFF.
-- Safe to re-run only if columns missing; duplicate column errors are OK to ignore.

ALTER TABLE `server_arena_config`
  ADD COLUMN `damage_scale_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT 'arena duel round damage/hit 2^n'
    AFTER `wager_currency`;

ALTER TABLE `server_arena_config`
  ADD COLUMN `allow_heal` TINYINT UNSIGNED NOT NULL DEFAULT 0
    COMMENT 'allow heal/revive skills in arena duel'
    AFTER `damage_scale_enabled`;

ALTER TABLE `server_arena_config`
  ADD COLUMN `allow_dodge` TINYINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT 'allow physical dodge roll in arena duel'
    AFTER `allow_heal`;

ALTER TABLE `server_arena_config`
  ADD COLUMN `allow_combat_pill` TINYINT UNSIGNED NOT NULL DEFAULT 0
    COMMENT 'allow 大力丸/神力丸 ATK%% in arena duel'
    AFTER `allow_dodge`;
