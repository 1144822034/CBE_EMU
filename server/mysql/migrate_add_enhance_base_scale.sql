-- Strengthen catalog-base M(L) knobs (admin 强化配置).
-- Server also CREATE TABLE IF NOT EXISTS on load.

CREATE TABLE IF NOT EXISTS server_enhance_base_scale (
  id TINYINT UNSIGNED NOT NULL DEFAULT 1,
  step_percent INT UNSIGNED NOT NULL DEFAULT 10,
  tier_extra_percent INT UNSIGNED NOT NULL DEFAULT 35,
  apply_attack INT UNSIGNED NOT NULL DEFAULT 100,
  apply_armor INT UNSIGNED NOT NULL DEFAULT 50,
  apply_strength INT UNSIGNED NOT NULL DEFAULT 50,
  apply_agility INT UNSIGNED NOT NULL DEFAULT 50,
  apply_wisdom INT UNSIGNED NOT NULL DEFAULT 50,
  apply_hp INT UNSIGNED NOT NULL DEFAULT 50,
  apply_mp INT UNSIGNED NOT NULL DEFAULT 50,
  apply_dodge INT UNSIGNED NOT NULL DEFAULT 0,
  apply_hit INT UNSIGNED NOT NULL DEFAULT 0,
  apply_crit INT UNSIGNED NOT NULL DEFAULT 0,
  apply_resist INT UNSIGNED NOT NULL DEFAULT 0,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO server_enhance_base_scale
  (id, step_percent, tier_extra_percent, apply_attack, apply_armor,
   apply_strength, apply_agility, apply_wisdom, apply_hp, apply_mp,
   apply_dodge, apply_hit, apply_crit, apply_resist)
VALUES (1, 10, 35, 100, 50, 50, 50, 50, 50, 50, 0, 0, 0, 0);
