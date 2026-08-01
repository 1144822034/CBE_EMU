USE `jh_online`;

-- Per-chest weighted reward pool for 522/523/524 (bronze/silver/gold).
-- Runtime also CREATE TABLE IF NOT EXISTS on load; admin edits hot-reload.
CREATE TABLE IF NOT EXISTS `server_chest_rewards` (
  `chest_item_id` INT UNSIGNED NOT NULL,
  `reward_item_id` INT UNSIGNED NOT NULL,
  `weight` INT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`chest_item_id`, `reward_item_id`)
) ENGINE=InnoDB;
