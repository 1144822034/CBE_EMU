-- Whole-server monster percent knobs (admin 怪物管理).
-- 100 = unchanged; 90 lowers; 110 raises. Service also auto-creates this table.

CREATE TABLE IF NOT EXISTS `server_monster_global_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `hp_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `attack_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `defense_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `drop_rate_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `reward_exp_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `reward_money_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO `server_monster_global_config` (
  `id`, `hp_percent`, `attack_percent`, `defense_percent`,
  `drop_rate_percent`, `reward_exp_percent`, `reward_money_percent`
) VALUES (1, 100, 100, 100, 100, 100, 100);
