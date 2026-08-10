USE `jh_online`;

ALTER TABLE `server_dynamic_npc_tasks`
  ADD COLUMN `daily_accept_limit` SMALLINT UNSIGNED NOT NULL DEFAULT 0
  COMMENT '0=不限制每日次数;N=每个自然日最多接取N次'
  AFTER `repeatable`;

CREATE TABLE IF NOT EXISTS `account_role_npc_task_daily` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `task_id` INT UNSIGNED NOT NULL,
  `day_ymd` INT UNSIGNED NOT NULL,
  `accept_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `scene`, `actor_id`, `task_id`, `day_ymd`),
  CONSTRAINT `fk_account_role_npc_task_daily_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;
