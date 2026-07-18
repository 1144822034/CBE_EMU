USE `jh_online`;

CREATE TABLE IF NOT EXISTS `account_role_tasks` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `task_id` INT UNSIGNED NOT NULL,
  `task_state` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '1=进行中,2=已完成',
  `progress1` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `progress2` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `task_id`),
  KEY `idx_account_role_tasks_state` (`account_id`, `role_id`, `task_state`),
  CONSTRAINT `fk_account_role_tasks_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;
