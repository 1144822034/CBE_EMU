-- MySQL 5.7+ migration for existing Jianghu OL mock-service deployments.
-- No default rows are inserted: client item.dsh does not contain historical
-- reward odds, so each chest must be deliberately configured in 宝箱管理.

CREATE TABLE IF NOT EXISTS `server_chest_rewards` (
  `chest_item_id` INT UNSIGNED NOT NULL,
  `reward_order` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  `weight` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`chest_item_id`, `reward_order`),
  KEY `idx_server_chest_rewards_item` (`item_id`)
) ENGINE=InnoDB;
