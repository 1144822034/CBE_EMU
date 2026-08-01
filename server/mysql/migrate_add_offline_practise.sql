-- Offline practise bank for item 827 修炼丹.
CREATE TABLE IF NOT EXISTS `account_role_offline_practise` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `bank_minutes` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_logout_unix` INT UNSIGNED NOT NULL DEFAULT 0,
  `today_ymd` INT UNSIGNED NOT NULL DEFAULT 0,
  `today_used_minutes` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_settle_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_settle_minutes` INT UNSIGNED NOT NULL DEFAULT 0,
  `is_gold` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`),
  CONSTRAINT `fk_account_role_offline_practise_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;
