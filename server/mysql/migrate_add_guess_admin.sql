-- 猜猜看后台开关 + 押注记录清理索引（已有库增量）
CREATE TABLE IF NOT EXISTS `server_guess_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_guess_config` (`id`, `enabled`) VALUES (1, 1);

-- 可选：加速按时间清理（索引已存在时忽略错误即可）
-- ALTER TABLE `server_guess_bets` ADD INDEX `idx_guess_bets_created` (`created_at`);
