USE `jh_online`;

CREATE TABLE IF NOT EXISTS `guilds` (
  `guild_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `guild_name` VARBINARY(12) NOT NULL,
  `leader_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `leader_role_id` INT UNSIGNED NOT NULL,
  `leader_role_name` VARBINARY(32) NOT NULL,
  `guild_level` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `minimum_level` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `member_limit` SMALLINT UNSIGNED NOT NULL DEFAULT 20,
  `guild_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `prosperity` INT UNSIGNED NOT NULL DEFAULT 0,
  `action_power` INT UNSIGNED NOT NULL DEFAULT 0,
  `research_power` INT UNSIGNED NOT NULL DEFAULT 0,
  `construction` INT UNSIGNED NOT NULL DEFAULT 0,
  `current_construction` VARBINARY(128) NOT NULL DEFAULT '',
  `notice` VARBINARY(60) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guild_id`),
  UNIQUE KEY `uk_guilds_name` (`guild_name`),
  KEY `idx_guilds_leader` (`leader_account_id`, `leader_role_id`),
  CONSTRAINT `fk_guilds_leader_role`
    FOREIGN KEY (`leader_account_id`, `leader_role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

-- 兼容已执行过早期迁移（外键曾是 RESTRICT）的开发库。
ALTER TABLE `guilds`
  DROP FOREIGN KEY `fk_guilds_leader_role`,
  ADD CONSTRAINT `fk_guilds_leader_role`
    FOREIGN KEY (`leader_account_id`, `leader_role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE;

CREATE TABLE IF NOT EXISTS `guild_members` (
  `guild_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `role_name` VARBINARY(32) NOT NULL,
  `member_rank` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT '1=帮主,2=管理,3=成员',
  `member_title` VARBINARY(20) NOT NULL DEFAULT '',
  `joined_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guild_id`, `account_id`, `role_id`),
  UNIQUE KEY `uk_guild_members_role` (`account_id`, `role_id`),
  KEY `idx_guild_members_role_id` (`role_id`),
  CONSTRAINT `fk_guild_members_guild`
    FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`guild_id`)
    ON DELETE CASCADE,
  CONSTRAINT `fk_guild_members_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `guild_applications` (
  `guild_id` INT UNSIGNED NOT NULL,
  `applicant_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `applicant_role_id` INT UNSIGNED NOT NULL,
  `applicant_role_name` VARBINARY(32) NOT NULL,
  `applicant_level` INT UNSIGNED NOT NULL,
  `applicant_job` TINYINT UNSIGNED NOT NULL,
  `applicant_sex` TINYINT UNSIGNED NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=待处理,1=同意,2=拒绝',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guild_id`, `applicant_account_id`, `applicant_role_id`),
  UNIQUE KEY `uk_guild_applications_role` (`applicant_account_id`, `applicant_role_id`),
  KEY `idx_guild_applications_pending` (`guild_id`, `status`, `created_at`),
  CONSTRAINT `fk_guild_applications_guild`
    FOREIGN KEY (`guild_id`) REFERENCES `guilds` (`guild_id`)
    ON DELETE CASCADE,
  CONSTRAINT `fk_guild_applications_role`
    FOREIGN KEY (`applicant_account_id`, `applicant_role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;
