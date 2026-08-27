-- Per-equipment instance id for wash restore (disambiguates same item_id+level).
USE `jh_online`;

ALTER TABLE `account_role_backpack`
  ADD COLUMN `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0
  AFTER `enhance_level`;

ALTER TABLE `account_role_equipment`
  ADD COLUMN `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0
  AFTER `enhance_level`;

ALTER TABLE `account_role_enhance_wash_record`
  ADD COLUMN `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0
  AFTER `role_id`;

ALTER TABLE `account_role_enhance_wash_record`
  ADD KEY `idx_role_item_instance` (`role_id`, `item_instance_id`);

ALTER TABLE `server_auction_listings`
  ADD COLUMN `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0
  AFTER `enhance_level`;

ALTER TABLE `server_auction_claims`
  ADD COLUMN `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0
  AFTER `enhance_level`;
