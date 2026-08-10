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

CREATE TABLE IF NOT EXISTS `server_payment_config` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `api_base_url` VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `secret_key` VARBINARY(128) NOT NULL,
  `callback_base_url` VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `wcoin_per_yuan` INT UNSIGNED NOT NULL DEFAULT 1000,
  `minimum_yuan` INT UNSIGNED NOT NULL DEFAULT 1,
  `maximum_yuan` INT UNSIGNED NOT NULL DEFAULT 10000,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_payment_config`
  (`config_id`, `api_base_url`, `secret_key`, `callback_base_url`,
   `wcoin_per_yuan`, `minimum_yuan`, `maximum_yuan`, `enabled`)
VALUES
  (1, 'http://pay.cbhub.top/', '', '', 1000, 1, 10000, 1);

CREATE TABLE IF NOT EXISTS `wcoin_recharge_orders` (
  `pay_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `provider_order_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NULL DEFAULT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `pay_type` TINYINT UNSIGNED NOT NULL,
  `price_cents` INT UNSIGNED NOT NULL,
  `really_price_cents` INT UNSIGNED NOT NULL DEFAULT 0,
  `wcoin_amount` INT UNSIGNED NOT NULL,
  `request_param` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `pay_url` VARCHAR(1024) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=创建中,1=待支付,2=已支付待入账,3=已入账,4=已过期,5=失败',
  `provider_state` SMALLINT NOT NULL DEFAULT 0,
  `timeout_minutes` SMALLINT UNSIGNED NOT NULL DEFAULT 5,
  `credited` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `last_checked_at` TIMESTAMP NULL DEFAULT NULL,
  `paid_at` TIMESTAMP NULL DEFAULT NULL,
  `credited_at` TIMESTAMP NULL DEFAULT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`pay_id`),
  UNIQUE KEY `uk_wcoin_recharge_provider_order` (`provider_order_id`),
  KEY `idx_wcoin_recharge_account` (`account_id`, `created_at`),
  KEY `idx_wcoin_recharge_pending` (`status`, `created_at`),
  CONSTRAINT `fk_wcoin_recharge_account`
    FOREIGN KEY (`account_id`) REFERENCES `accounts` (`account_id`)
    ON DELETE RESTRICT
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_data_migrations` (
  `migration_name` VARCHAR(127) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`migration_name`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_login_servers` (
  `server_id` INT UNSIGNED NOT NULL,
  `display_name` VARBINARY(31) NOT NULL,
  `status_label` VARBINARY(31) NOT NULL,
  `display_color` MEDIUMINT UNSIGNED NOT NULL DEFAULT 16777215,
  `sort_order` INT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`server_id`),
  KEY `idx_server_login_servers_visible` (`enabled`, `sort_order`, `server_id`)
) ENGINE=InnoDB;

-- GBK: 江湖一区 / 推荐. This is bootstrap data, not a mock packet fallback.
INSERT IGNORE INTO `server_login_servers`
  (`server_id`, `display_name`, `status_label`, `display_color`, `sort_order`, `enabled`)
VALUES
  (1, X'BDADBAFED2BBC7F8', X'CDC6BCF6', 16777215, 0, 1);

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
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `durability` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `durability_max` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
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
  `durability` SMALLINT UNSIGNED NOT NULL,
  `durability_max` SMALLINT UNSIGNED NOT NULL,
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
  `durability` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '装备实例当前耐久；非装备为 0',
  `durability_max` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '装备实例最大耐久；非装备为 0',
  PRIMARY KEY (`account_id`, `role_id`, `slot_index`),
  KEY `idx_account_role_backpack_item` (`account_id`, `role_id`, `item_id`, `item_seq`),
  CONSTRAINT `fk_account_role_backpack_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

-- item.dsh declares 921（修炼天书）as a non-stackable, sequence-owned
-- instance. Its static description is intentionally empty: the client reads
-- this durable per-instance record through 7/38 and 7/40.
CREATE TABLE IF NOT EXISTS `account_role_training_books` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `item_seq` SMALLINT UNSIGNED NOT NULL,
  `title` VARBINARY(48) NOT NULL,
  `book_description` VARBINARY(200) NOT NULL,
  `book_info` VARBINARY(200) NOT NULL,
  `book_level` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `book_experience` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `item_seq`),
  CONSTRAINT `fk_account_role_training_books_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

-- Timed special items are separate from the durable role snapshot because
-- expiry continues while the character is offline.  The item id and
-- multiplier preserve the server-authoritative effect rather than trusting a
-- client-side icon or countdown.
CREATE TABLE IF NOT EXISTS `account_role_item_effects` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `effect_kind` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `multiplier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `expires_unix` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `effect_kind`),
  KEY `idx_account_role_item_effects_expiry` (`expires_unix`),
  CONSTRAINT `fk_account_role_item_effects_role`
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

-- A task.dsh-compatible first reward remains mirrored in server_tasks.  This
-- relation is authoritative when rows exist and preserves the deterministic
-- award order consumed by the client's 6/4 awardinfo parser.
CREATE TABLE IF NOT EXISTS `server_task_reward_items` (
  `task_id` INT UNSIGNED NOT NULL,
  `reward_order` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  `item_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`task_id`, `reward_order`),
  KEY `idx_server_task_reward_items_item` (`item_id`)
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
  `service_option_name` VARBINARY(64) NOT NULL DEFAULT '',
  `service_option_description` VARBINARY(96) NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_dynamic_npc_tasks` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `task_id` INT UNSIGNED NOT NULL,
  `repeatable` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=不可重复,1=完成后可重新接取',
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

-- Direct NPC dialog services are a set, not a replacement for task bindings.
-- service_kind=0 is the explicit “configured but no direct service” marker;
-- 1..7 are the existing parser-backed action=1 service kinds.  Rows are
-- shared by dynamic NPCs and scene-native NPC overrides, so no foreign key is
-- used here: native actors do not have a server_dynamic_npcs parent row.
CREATE TABLE IF NOT EXISTS `server_npc_services` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `service_kind` SMALLINT UNSIGNED NOT NULL,
  `sort_order` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `option_name` VARBINARY(64) NOT NULL DEFAULT '',
  `option_description` VARBINARY(96) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`, `service_kind`),
  KEY `idx_server_npc_services_dialog` (`scene`, `actor_id`, `sort_order`, `service_kind`)
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

-- One independent probability row per monster drop.  The two legacy drop_*
-- columns above remain only as a compatibility import source for old installs;
-- new backend saves write this table and clear the legacy columns.
CREATE TABLE IF NOT EXISTS `server_monster_drops` (
  `monster_id` SMALLINT UNSIGNED NOT NULL,
  `drop_slot` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `drop_rate_percent` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`monster_id`, `drop_slot`),
  CONSTRAINT `fk_server_monster_drops_monster`
    FOREIGN KEY (`monster_id`) REFERENCES `server_monsters` (`monster_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_shop_items` (
  `item_id` INT UNSIGNED NOT NULL,
  `price` INT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `shop_section` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`item_id`)
) ENGINE=InnoDB;

-- The three chest/key identities are read from item.dsh; its data contains no
-- reward table or rates.  This server-authoritative weighted pool selects one
-- row per successful opening.  An empty chest has no configured opening and
-- is intentionally not consumed.
CREATE TABLE IF NOT EXISTS `server_chest_rewards` (
  `chest_item_id` INT UNSIGNED NOT NULL,
  `reward_order` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  `weight` INT UNSIGNED NOT NULL,
  `world_broadcast` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`chest_item_id`, `reward_order`),
  KEY `idx_server_chest_rewards_item` (`item_id`)
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
