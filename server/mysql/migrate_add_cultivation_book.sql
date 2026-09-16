-- 修炼天书实例状态（920 空白 → 921 记录本）。
-- book_id 写入背包行 enhance_instance_id；跨交易随道具迁移。
CREATE TABLE IF NOT EXISTS `account_cultivation_books` (
  `book_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `book_level` INT UNSIGNED NOT NULL DEFAULT 0,
  `capacity_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `filled_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `creator_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `creator_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`book_id`),
  KEY `idx_cultivation_books_creator` (`creator_account_id`, `creator_role_id`)
) ENGINE=InnoDB;
