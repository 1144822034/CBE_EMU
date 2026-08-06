-- MySQL 5.7+/8.0 compatible migration for configurable multi-item task rewards.
-- Existing server_tasks.reward_item_* values remain untouched and continue to
-- serve as the fallback first reward until an administrator saves the task.

CREATE TABLE IF NOT EXISTS `server_task_reward_items` (
  `task_id` INT UNSIGNED NOT NULL,
  `reward_order` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  `item_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`task_id`, `reward_order`),
  KEY `idx_server_task_reward_items_item` (`item_id`)
) ENGINE=InnoDB;
