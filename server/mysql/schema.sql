CREATE DATABASE IF NOT EXISTS `jh_online`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE `jh_online`;

CREATE TABLE IF NOT EXISTS `accounts` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `password_value` VARBINARY(64) NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_admin_config` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `password_value` VARBINARY(64) NOT NULL,
  `failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `locked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_admin_config`
  (`config_id`, `password_value`, `failed_attempts`, `locked`)
VALUES
  (1, '123456', 0, 0);

CREATE TABLE IF NOT EXISTS `server_data_migrations` (
  `migration_name` VARCHAR(127) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`migration_name`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `world_chat_messages` (
  `message_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `source_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `source_role_id` INT UNSIGNED NOT NULL,
  `source_name` VARBINARY(15) NOT NULL,
  `message` VARBINARY(81) NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`message_id`),
  KEY `idx_world_chat_source` (`source_account_id`, `source_role_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `friendships` (
  `owner_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `owner_role_id` INT UNSIGNED NOT NULL,
  `target_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `target_role_id` INT UNSIGNED NOT NULL,
  `target_role_name` VARBINARY(32) NOT NULL,
  `friend_degree` INT UNSIGNED NOT NULL DEFAULT 1,
  `target_level` INT UNSIGNED NOT NULL DEFAULT 1,
  `target_job` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `target_sex` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_account_id`, `owner_role_id`, `target_account_id`, `target_role_id`),
  KEY `idx_friendships_target` (`target_account_id`, `target_role_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_state` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `format_version` INT UNSIGNED NOT NULL,
  `active_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `role_count` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_roles` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `role_index` TINYINT UNSIGNED NOT NULL,
  `role_name` VARBINARY(32) NOT NULL,
  `job` TINYINT UNSIGNED NOT NULL,
  `sex` TINYINT UNSIGNED NOT NULL,
  `backpack_capacity` TINYINT UNSIGNED NOT NULL,
  `level` INT UNSIGNED NOT NULL,
  `exp` INT UNSIGNED NOT NULL,
  `hp` INT UNSIGNED NOT NULL,
  `hp_max` INT UNSIGNED NOT NULL,
  `mp` INT UNSIGNED NOT NULL,
  `mp_max` INT UNSIGNED NOT NULL,
  `money` INT UNSIGNED NOT NULL,
  `wcoin` INT UNSIGNED NOT NULL,
  `scene` VARBINARY(64) NOT NULL,
  `pos_x` SMALLINT UNSIGNED NOT NULL,
  `pos_y` SMALLINT UNSIGNED NOT NULL,
  `backpack_item_count` TINYINT UNSIGNED NOT NULL,
  `designation_id` TINYINT UNSIGNED NOT NULL,
  `next_backpack_seq` SMALLINT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`),
  UNIQUE KEY `uk_account_roles_role_id` (`role_id`),
  UNIQUE KEY `uk_account_roles_index` (`account_id`, `role_index`),
  CONSTRAINT `fk_account_roles_state`
    FOREIGN KEY (`account_id`) REFERENCES `account_role_state` (`account_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_equipment` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `slot_index` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`account_id`, `role_id`, `slot_index`),
  CONSTRAINT `fk_account_role_equipment_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_equipment_durability` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `slot_index` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `durability` SMALLINT UNSIGNED NOT NULL DEFAULT 100,
  `durability_max` SMALLINT UNSIGNED NOT NULL DEFAULT 100,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `slot_index`),
  CONSTRAINT `fk_account_role_equipment_durability_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_skills` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `skill_id` INT UNSIGNED NOT NULL,
  `skill_level` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `learned_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `skill_id`),
  CONSTRAINT `fk_account_role_skills_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_backpack` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `slot_index` SMALLINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_seq` SMALLINT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL COMMENT '普通物品为堆叠数；802/803 为剩余 HP/MP 储量',
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`account_id`, `role_id`, `slot_index`),
  KEY `idx_account_role_backpack_item` (`account_id`, `role_id`, `item_id`, `item_seq`),
  CONSTRAINT `fk_account_role_backpack_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

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

CREATE TABLE IF NOT EXISTS `server_tasks` (
  `task_id` INT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `difficulty` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `classification` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `requirement_type1` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=无,1=物品,2=怪物',
  `requirement_count1` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `requirement_id1` INT UNSIGNED NOT NULL DEFAULT 0,
  `requirement_type2` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=无,1=物品,2=怪物',
  `requirement_count2` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `requirement_id2` INT UNSIGNED NOT NULL DEFAULT 0,
  `prerequisite_task_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `given_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `given_item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `name` VARBINARY(31) NOT NULL,
  `giver` VARBINARY(15) NOT NULL,
  `receiver` VARBINARY(15) NOT NULL,
  `goal` VARBINARY(95) NOT NULL DEFAULT '',
  `reward_text` VARBINARY(31) NOT NULL DEFAULT '',
  `offer_dialog` VARBINARY(255) NOT NULL DEFAULT '',
  `active_dialog` VARBINARY(255) NOT NULL DEFAULT '',
  `completed_dialog` VARBINARY(255) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`task_id`),
  KEY `idx_server_tasks_enabled` (`enabled`, `task_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `role_id_sequence` (
  `role_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`role_id`)
) ENGINE=InnoDB AUTO_INCREMENT=10001;

CREATE TABLE IF NOT EXISTS `server_dynamic_npcs` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `pos_x` SMALLINT UNSIGNED NOT NULL,
  `pos_y` SMALLINT UNSIGNED NOT NULL,
  `npc_kind` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `orientation` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `actor_resource` VARBINARY(64) NOT NULL,
  `display_name` VARBINARY(32) NOT NULL,
  `script_name` VARBINARY(64) NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_dynamic_npc_tasks` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `task_id` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`),
  KEY `idx_server_dynamic_npc_tasks_task` (`task_id`),
  CONSTRAINT `fk_server_dynamic_npc_tasks_npc`
    FOREIGN KEY (`scene`, `actor_id`)
    REFERENCES `server_dynamic_npcs` (`scene`, `actor_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_dynamic_npc_instances` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `target_scene` VARBINARY(64) NOT NULL DEFAULT '',
  `target_x` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `target_y` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `challenge_enemy_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `minimum_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`),
  CONSTRAINT `fk_server_dynamic_npc_instances_npc`
    FOREIGN KEY (`scene`, `actor_id`)
    REFERENCES `server_dynamic_npcs` (`scene`, `actor_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_monsters` (
  `monster_id` SMALLINT UNSIGNED NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  `family` TINYINT UNSIGNED NOT NULL,
  `hp` INT UNSIGNED NOT NULL,
  `mp` INT UNSIGNED NOT NULL,
  `attack_value` INT UNSIGNED NOT NULL,
  `defense_value` INT UNSIGNED NOT NULL,
  `reward_exp` INT UNSIGNED NOT NULL,
  `reward_money` INT UNSIGNED NOT NULL,
  `drop_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `drop_rate_percent` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`monster_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_shop_items` (
  `item_id` INT UNSIGNED NOT NULL,
  `price` INT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`item_id`)
) ENGINE=InnoDB;

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

-- 仅供旧 payload 一次性迁移或灾难恢复；正常运行不再写入此表。
CREATE TABLE IF NOT EXISTS `account_role_state_payload_backup` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `format_version` INT UNSIGNED NOT NULL,
  `active_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `role_count` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `payload` LONGBLOB NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;
