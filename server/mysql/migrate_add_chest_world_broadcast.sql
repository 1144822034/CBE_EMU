-- MySQL 5.7+ migration for deployments that already created
-- server_chest_rewards before reward-level world broadcasts existed.
--
-- The server performs the same information_schema-guarded ALTER at startup,
-- so applying this file is optional for normal upgrades.  Run it once only
-- when managing schema changes manually; MySQL 5.7 has no portable
-- ADD COLUMN IF NOT EXISTS form.

ALTER TABLE `server_chest_rewards`
  ADD COLUMN `world_broadcast` TINYINT UNSIGNED NOT NULL DEFAULT 0
  AFTER `weight`;
