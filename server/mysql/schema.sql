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

CREATE TABLE IF NOT EXISTS `server_registration_daily` (
  `day_ymd` INT UNSIGNED NOT NULL COMMENT 'YYYYMMDD local server day',
  `register_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`day_ymd`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_registration_config` (
  `id` TINYINT UNSIGNED NOT NULL,
  `daily_limit` INT UNSIGNED NOT NULL DEFAULT 3,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_registration_config` (`id`, `daily_limit`) VALUES (1, 3);

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

-- Physical gateway zones (Pixi host-layer). Not title WT 1/1/12 rows.
CREATE TABLE IF NOT EXISTS `server_gateway_zones` (
  `zone_id` INT UNSIGNED NOT NULL,
  `display_name` VARBINARY(63) NOT NULL,
  `status_label` VARBINARY(63) NOT NULL,
  `ws_url` VARBINARY(191) NOT NULL,
  `account_web_url` VARBINARY(191) NOT NULL,
  `sort_order` INT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `is_default` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`zone_id`),
  KEY `idx_server_gateway_zones_visible` (`enabled`, `sort_order`, `zone_id`)
) ENGINE=InnoDB;

-- UTF-8: 江湖归来 / 推荐 / ws://192.1.1.1:19092. Empty-install seed only.
INSERT IGNORE INTO `server_gateway_zones`
  (`zone_id`, `display_name`, `status_label`, `ws_url`, `account_web_url`,
   `sort_order`, `enabled`, `is_default`)
VALUES
  (1,
   X'E6B19FE6B996E5BD92E69DA5',
   X'E68EA8E88D90',
   X'77733A2F2F3139322E312E312E313A3139303932',
   X'',
   0, 1, 1);

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
  `last_team_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_decay_at` TIMESTAMP NULL DEFAULT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`owner_account_id`, `owner_role_id`, `target_account_id`, `target_role_id`),
  KEY `idx_friendships_target` (`target_account_id`, `target_role_id`)
) ENGINE=InnoDB;

/* Pixi 恩怨：切磋/决斗击杀累计。 */
CREATE TABLE IF NOT EXISTS `enmity_kills` (
  `killer_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `killer_role_id` INT UNSIGNED NOT NULL,
  `killer_role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `victim_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `victim_role_id` INT UNSIGNED NOT NULL,
  `victim_role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `kill_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`killer_account_id`, `killer_role_id`, `victim_account_id`, `victim_role_id`),
  KEY `idx_enmity_victim` (`victim_account_id`, `victim_role_id`),
  KEY `idx_enmity_killer` (`killer_account_id`, `killer_role_id`)
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
  `backpack_capacity` SMALLINT UNSIGNED NOT NULL,
  `level` INT UNSIGNED NOT NULL,
  `exp` INT UNSIGNED NOT NULL,
  `hp` INT UNSIGNED NOT NULL,
  `hp_max` INT UNSIGNED NOT NULL,
  `mp` INT UNSIGNED NOT NULL,
  `mp_max` INT UNSIGNED NOT NULL,
  `money` INT UNSIGNED NOT NULL,
  `wcoin` INT UNSIGNED NOT NULL,
  `pk_points` INT UNSIGNED NOT NULL DEFAULT 0,
  `pk_pve_streak` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `scene` VARBINARY(64) NOT NULL,
  `pos_x` SMALLINT UNSIGNED NOT NULL,
  `pos_y` SMALLINT UNSIGNED NOT NULL,
  `backpack_item_count` SMALLINT UNSIGNED NOT NULL,
  `designation_id` TINYINT UNSIGNED NOT NULL,
  `arena_designation_mask` INT UNSIGNED NOT NULL DEFAULT 0,
  `next_backpack_seq` SMALLINT UNSIGNED NOT NULL,
  `spouse_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `spouse_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `spouse_name` VARBINARY(32) NOT NULL DEFAULT '',
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
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
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
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  PRIMARY KEY (`account_id`, `role_id`, `slot_index`),
  KEY `idx_account_role_backpack_item` (`account_id`, `role_id`, `item_id`, `item_seq`),
  CONSTRAINT `fk_account_role_backpack_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

-- Last wash snapshot keyed by item_instance_id (legacy bag_seq fallback).
CREATE TABLE IF NOT EXISTS `account_role_enhance_wash_record` (
  `role_id` INT UNSIGNED NOT NULL,
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `bag_seq` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `unlock_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `washed_unix` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`role_id`, `bag_seq`),
  KEY `idx_role_item_instance` (`role_id`, `item_instance_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Dialog-driven player warehouse (mall pass 834). Not the unresolved native 钱庄.
CREATE TABLE IF NOT EXISTS `account_role_warehouse` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `slot_index` SMALLINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_seq` SMALLINT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `slot_index`),
  KEY `idx_account_role_warehouse_item` (`account_id`, `role_id`, `item_id`, `item_seq`),
  CONSTRAINT `fk_account_role_warehouse_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

-- Warehouse gold bank (copper; UI unit is 金=10000铜). Separate from item slots
-- so empty warehouses still keep stored money across item DELETE/INSERT.
CREATE TABLE IF NOT EXISTS `account_role_warehouse_bank` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `money` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`),
  CONSTRAINT `fk_account_role_warehouse_bank_role`
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

-- Offline practise bank for 修炼丹 (827). Wall-clock logout stamp + bank
-- minutes survive disconnect; login settles exp = minutes * level * 8.
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
  `npc_service_mask` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'bits 1..6 = enabled root services; 0=derive from npc_kind',
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
  `repeatable` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=不可重复,1=完成后可重新接取',
  `daily_accept_limit` SMALLINT UNSIGNED NOT NULL DEFAULT 0
    COMMENT '0=不限制每日次数;N=每个自然日最多接取N次',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`, `task_id`),
  KEY `idx_server_dynamic_npc_tasks_task` (`task_id`),
  CONSTRAINT `fk_server_dynamic_npc_tasks_npc`
    FOREIGN KEY (`scene`, `actor_id`)
    REFERENCES `server_dynamic_npcs` (`scene`, `actor_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

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

CREATE TABLE IF NOT EXISTS `server_dynamic_npc_instances` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `target_scene` VARBINARY(64) NOT NULL DEFAULT '',
  `target_x` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `target_y` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `challenge_enemy_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `minimum_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `teleport_require_task_id` INT UNSIGNED NOT NULL DEFAULT 0
    COMMENT '0=always show teleport; else show after this task completed(state3)',
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
  `cast_skill` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `actor_resource` VARBINARY(63) NOT NULL DEFAULT '',
  `display_name` VARBINARY(31) NOT NULL DEFAULT '',
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

-- Admin-created custom monsters (not present in the builtin/SCE catalog).
CREATE TABLE IF NOT EXISTS `server_monster_catalog_extra` (
  `monster_id` SMALLINT UNSIGNED NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  `family` TINYINT UNSIGNED NOT NULL,
  `display_name` VARBINARY(31) NOT NULL,
  `source_label` VARBINARY(63) NOT NULL,
  `actor_resource` VARBINARY(63) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`monster_id`)
) ENGINE=InnoDB;

-- Per-scene override of automonster.dsh three monster slots.
CREATE TABLE IF NOT EXISTS `server_scene_monsters` (
  `scene` VARBINARY(63) NOT NULL,
  `monster_id_1` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `monster_id_2` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `monster_id_3` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`)
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

-- 比武擂台 (Pixi) — also ensured at runtime by mock_server_arena.c
CREATE TABLE IF NOT EXISTS `server_arena_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `scene` VARBINARY(64) NOT NULL,
  `spawn_x` SMALLINT UNSIGNED NOT NULL DEFAULT 120,
  `spawn_y` SMALLINT UNSIGNED NOT NULL DEFAULT 120,
  `weekday_mask` INT UNSIGNED NOT NULL DEFAULT 126 COMMENT 'bit0=Sun..bit6=Sat; derived from weekday_brackets',
  `weekday_brackets` VARCHAR(32) NOT NULL DEFAULT '0,70,50,10,70,50,10' COMMENT 'Sun..Sat: 0=rest,10/50/70 one bracket per day',
  `entry_start_minute` SMALLINT UNSIGNED NOT NULL DEFAULT 1220,
  `entry_end_minute` SMALLINT UNSIGNED NOT NULL DEFAULT 1229,
  `match_start_minute` SMALLINT UNSIGNED NOT NULL DEFAULT 1230,
  `match_end_minute` SMALLINT UNSIGNED NOT NULL DEFAULT 1260,
  `round_seconds` INT UNSIGNED NOT NULL DEFAULT 300,
  `battle_timeout_seconds` INT UNSIGNED NOT NULL DEFAULT 240,
  `score_win` INT UNSIGNED NOT NULL DEFAULT 20,
  `score_bye` INT UNSIGNED NOT NULL DEFAULT 20,
  `score_draw` INT UNSIGNED NOT NULL DEFAULT 10,
  `npc_name_keyword` VARBINARY(32) NOT NULL DEFAULT '',
  `wager_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `wager_fee_percent` TINYINT UNSIGNED NOT NULL DEFAULT 10,
  `wager_currency` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=铜币',
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_rewards` (
  `job` TINYINT UNSIGNED NOT NULL,
  `bracket` SMALLINT UNSIGNED NOT NULL,
  `rank_pos` TINYINT UNSIGNED NOT NULL,
  `reward_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_wcoin` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `equip_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`job`, `bracket`, `rank_pos`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_sessions` (
  `session_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `day_ymd` INT UNSIGNED NOT NULL,
  `slot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=entry 1=live 2=settled',
  `entry_start_ts` INT UNSIGNED NOT NULL DEFAULT 0,
  `match_start_ts` INT UNSIGNED NOT NULL DEFAULT 0,
  `match_end_ts` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`session_id`),
  UNIQUE KEY `uk_arena_sessions_day_slot` (`day_ymd`, `slot`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_scores` (
  `session_id` INT UNSIGNED NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_name` VARBINARY(32) NOT NULL,
  `job` TINYINT UNSIGNED NOT NULL,
  `bracket` SMALLINT UNSIGNED NOT NULL,
  `level` INT UNSIGNED NOT NULL,
  `score` INT UNSIGNED NOT NULL DEFAULT 0,
  `wins` INT UNSIGNED NOT NULL DEFAULT 0,
  `losses` INT UNSIGNED NOT NULL DEFAULT 0,
  `draws` INT UNSIGNED NOT NULL DEFAULT 0,
  `byes` INT UNSIGNED NOT NULL DEFAULT 0,
  `rank_pos` INT UNSIGNED NOT NULL DEFAULT 0,
  `claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`session_id`, `role_id`),
  KEY `idx_arena_scores_bucket` (`session_id`, `job`, `bracket`, `score`),
  CONSTRAINT `fk_arena_scores_session`
    FOREIGN KEY (`session_id`) REFERENCES `server_arena_sessions` (`session_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_titles` (
  `job` TINYINT UNSIGNED NOT NULL,
  `bracket` SMALLINT UNSIGNED NOT NULL,
  `place` TINYINT UNSIGNED NOT NULL COMMENT '1冠军 2亚军 3季军',
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_name` VARBINARY(32) NOT NULL,
  `session_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`job`, `bracket`, `place`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_wager_challenges` (
  `challenge_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `host_role_id` INT UNSIGNED NOT NULL,
  `host_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `challenger_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `challenger_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `scene` VARBINARY(64) NOT NULL,
  `stake_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `fee_percent` TINYINT UNSIGNED NOT NULL DEFAULT 10,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=open 1=matched 2=done 3=cancel',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`challenge_id`),
  KEY `idx_arena_wager_host` (`host_role_id`, `status`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_wager_members` (
  `challenge_id` INT UNSIGNED NOT NULL,
  `side` TINYINT UNSIGNED NOT NULL COMMENT '0=host 1=challenger',
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `paid` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `accepted` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `payout` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`challenge_id`, `role_id`),
  KEY `idx_wager_member_role` (`role_id`)
) ENGINE=InnoDB;


CREATE TABLE IF NOT EXISTS `server_enhance_milestones` (
  `equip_slot` TINYINT UNSIGNED NOT NULL COMMENT '0武器..7饰品',
  `unlock_level` TINYINT UNSIGNED NOT NULL COMMENT '4/8/12/16',
  `attr_type` TINYINT UNSIGNED NOT NULL COMMENT '1力..10法力',
  `flag` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0值 1%',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `value_q0` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质0默认档数值',
  `value_q1` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质1默认档数值',
  `value_q2` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质2默认档数值',
  `value_q3` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质3默认档数值',
  `value_q4` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质4默认档数值',
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`equip_slot`, `unlock_level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `server_enhance_attr_pool` (
  `attr_type` TINYINT UNSIGNED NOT NULL,
  `flag` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `value_min` INT UNSIGNED NOT NULL DEFAULT 1,
  `value_max` INT UNSIGNED NOT NULL DEFAULT 1,
  `weight` INT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`attr_type`, `flag`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `server_auction_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `fee_percent_base` TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `fee_percent_per_tier` TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `min_bid_step` INT UNSIGNED NOT NULL DEFAULT 1,
  `currency_mask` TINYINT UNSIGNED NOT NULL DEFAULT 3,
  `max_active_listings` INT UNSIGNED NOT NULL DEFAULT 2,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_auction_listings` (
  `listing_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `seller_role_id` INT UNSIGNED NOT NULL,
  `seller_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `seller_name` VARBINARY(32) NOT NULL DEFAULT '',
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `start_price` INT UNSIGNED NOT NULL DEFAULT 0,
  `current_price` INT UNSIGNED NOT NULL DEFAULT 0,
  `buyout_price` INT UNSIGNED NOT NULL DEFAULT 0,
  `currency` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `fee_paid` INT UNSIGNED NOT NULL DEFAULT 0,
  `fee_percent` TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `duration_tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `high_bidder_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `high_bidder_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `expire_unix` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`listing_id`),
  KEY `idx_auction_listings_status_expire` (`status`, `expire_unix`),
  KEY `idx_auction_listings_seller` (`seller_role_id`, `status`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_auction_bids` (
  `bid_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `listing_id` INT UNSIGNED NOT NULL,
  `bidder_role_id` INT UNSIGNED NOT NULL,
  `bidder_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `currency` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`bid_id`),
  KEY `idx_auction_bids_listing` (`listing_id`, `status`),
  KEY `idx_auction_bids_bidder` (`bidder_role_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_auction_claims` (
  `claim_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `kind` TINYINT UNSIGNED NOT NULL,
  `listing_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `money_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `wcoin_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`claim_id`),
  KEY `idx_auction_claims_role` (`role_id`, `claimed`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_combat_formula_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `panel_defense_base` INT UNSIGNED NOT NULL DEFAULT 4,
  `panel_defense_level_coeff` INT UNSIGNED NOT NULL DEFAULT 1,
  `panel_defense_endurance_div` INT UNSIGNED NOT NULL DEFAULT 2,
  `panel_hit_base` INT UNSIGNED NOT NULL DEFAULT 101,
  `primary_attack_coeff_tianji_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `primary_attack_coeff_huanjian_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `primary_attack_coeff_guidao_percent` INT UNSIGNED NOT NULL DEFAULT 100,
  `soft_armor_k` INT UNSIGNED NOT NULL DEFAULT 2000,
  `equip_armor_divisor` INT UNSIGNED NOT NULL DEFAULT 5,
  `hit_chance_min` INT UNSIGNED NOT NULL DEFAULT 5,
  `hit_chance_max` INT UNSIGNED NOT NULL DEFAULT 95,
  `crit_damage_percent` INT UNSIGNED NOT NULL DEFAULT 150,
  `resist_cap_percent` INT UNSIGNED NOT NULL DEFAULT 70,
  `job_skill_attack_bonus_percent` INT UNSIGNED NOT NULL DEFAULT 5,
  `na_variance_percent` INT UNSIGNED NOT NULL DEFAULT 5,
  `skill_variance_percent` INT UNSIGNED NOT NULL DEFAULT 8,
  `duel_job_bonus_tianji_percent` INT UNSIGNED NOT NULL DEFAULT 10,
  `duel_job_bonus_huanjian_percent` INT UNSIGNED NOT NULL DEFAULT 8,
  `duel_job_bonus_guidao_percent` INT UNSIGNED NOT NULL DEFAULT 12,
  `skill_vs_na_floor_percent` INT UNSIGNED NOT NULL DEFAULT 90,
  `monster_hit_default` INT UNSIGNED NOT NULL DEFAULT 100,
  `monster_dodge_default` INT UNSIGNED NOT NULL DEFAULT 0,
  `monster_crit_default` INT UNSIGNED NOT NULL DEFAULT 0,
  `monster_resist_default` INT UNSIGNED NOT NULL DEFAULT 0,
  `boss_skill_chance_percent` INT UNSIGNED NOT NULL DEFAULT 45,
  `boss_skill_damage_percent` INT UNSIGNED NOT NULL DEFAULT 300,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_world_boss_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `monster_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `announce_seconds` SMALLINT UNSIGNED NOT NULL DEFAULT 30,
  `reward_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

/* Live world-boss anchor — DB/runtime only; never written into stock .sce. */
CREATE TABLE IF NOT EXISTS `server_world_boss_runtime` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `scene` VARBINARY(63) NOT NULL DEFAULT '',
  `x` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `y` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `wire_id` INT UNSIGNED NOT NULL DEFAULT 250,
  `hour_key` INT UNSIGNED NOT NULL DEFAULT 0,
  `spawn_unix` INT UNSIGNED NOT NULL DEFAULT 0,
  `claim_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `preview_scene` VARBINARY(63) NOT NULL DEFAULT '',
  `preview_unix` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_ladder_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `daily_limit` SMALLINT UNSIGNED NOT NULL DEFAULT 1,
  `enemy_id_tianji` INT UNSIGNED NOT NULL DEFAULT 920001,
  `enemy_id_huanjian` INT UNSIGNED NOT NULL DEFAULT 920002,
  `enemy_id_guidao` INT UNSIGNED NOT NULL DEFAULT 920003,
  `base_hp` INT UNSIGNED NOT NULL DEFAULT 800,
  `base_atk` INT UNSIGNED NOT NULL DEFAULT 120,
  `base_def` INT UNSIGNED NOT NULL DEFAULT 80,
  `base_mp` INT UNSIGNED NOT NULL DEFAULT 200,
  `hp_per_floor` INT UNSIGNED NOT NULL DEFAULT 150,
  `atk_per_floor` INT UNSIGNED NOT NULL DEFAULT 25,
  `def_per_floor` INT UNSIGNED NOT NULL DEFAULT 15,
  `damage_scale_per_floor` SMALLINT UNSIGNED NOT NULL DEFAULT 5,
  `cast_skill` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_ladder_rewards` (
  `floor_milestone` SMALLINT UNSIGNED NOT NULL,
  `reward_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`floor_milestone`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_ladder_progress` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `best_floor` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_ladder_daily` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `day_ymd` INT UNSIGNED NOT NULL,
  `attempt_count` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `best_floor` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `day_ymd`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_guess_rounds` (
  `round_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `period_start_unix` INT UNSIGNED NOT NULL,
  `period_end_unix` INT UNSIGNED NOT NULL,
  `dice1` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `dice2` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `dice3` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`round_id`),
  UNIQUE KEY `idx_guess_rounds_period` (`period_start_unix`),
  KEY `idx_guess_rounds_end` (`status`, `period_end_unix`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_guess_bets` (
  `bet_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `round_id` INT UNSIGNED NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `choice` TINYINT UNSIGNED NOT NULL,
  `stake_copper` INT UNSIGNED NOT NULL,
  `payout_copper` INT UNSIGNED NOT NULL DEFAULT 0,
  `win` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`bet_id`),
  KEY `idx_guess_bets_role` (`role_id`, `bet_id` DESC),
  KEY `idx_guess_bets_round` (`round_id`, `win`, `payout_copper`),
  KEY `idx_guess_bets_created` (`created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_guess_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

-- Vitality / festival envoy / Yanmen materials / ring quest (WT46/47).
CREATE TABLE IF NOT EXISTS `account_role_vitality` (
  `account_id` VARBINARY(63) NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `vitality` INT UNSIGNED NOT NULL DEFAULT 0,
  `vitality_max` INT UNSIGNED NOT NULL DEFAULT 10000,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_festival_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `npc_keyword` VARBINARY(32) NOT NULL DEFAULT '',
  `welcome_text` VARBINARY(128) NOT NULL DEFAULT '',
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_festival_exchange` (
  `exchange_id` INT UNSIGNED NOT NULL,
  `vitality_cost` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `label` VARBINARY(48) NOT NULL DEFAULT '',
  `sort_order` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`exchange_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_yanmen_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `scene_keyword` VARBINARY(32) NOT NULL DEFAULT '',
  `npc_keyword` VARBINARY(32) NOT NULL DEFAULT '',
  `wood_item_id` INT UNSIGNED NOT NULL DEFAULT 850,
  `stone_item_id` INT UNSIGNED NOT NULL DEFAULT 851,
  `wood_drop_min` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `wood_drop_max` TINYINT UNSIGNED NOT NULL DEFAULT 3,
  `stone_drop_min` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `stone_drop_max` TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_yanmen_exchange` (
  `exchange_id` INT UNSIGNED NOT NULL,
  `wood_cost` INT UNSIGNED NOT NULL DEFAULT 0,
  `stone_cost` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `label` VARBINARY(48) NOT NULL DEFAULT '',
  `sort_order` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`exchange_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_ring_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `npc_keyword` VARBINARY(32) NOT NULL DEFAULT '',
  `daily_max` SMALLINT UNSIGNED NOT NULL DEFAULT 20,
  `daily_bonus_item_id` INT UNSIGNED NOT NULL DEFAULT 905,
  `daily_bonus_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `key_drop_rate` TINYINT UNSIGNED NOT NULL DEFAULT 5,
  `key_item_id1` INT UNSIGNED NOT NULL DEFAULT 813,
  `key_item_id2` INT UNSIGNED NOT NULL DEFAULT 814,
  `key_item_id3` INT UNSIGNED NOT NULL DEFAULT 815,
  `base_exp` INT UNSIGNED NOT NULL DEFAULT 500,
  `base_money` INT UNSIGNED NOT NULL DEFAULT 1000,
  `exp_per_ring` INT UNSIGNED NOT NULL DEFAULT 200,
  `money_per_ring` INT UNSIGNED NOT NULL DEFAULT 500,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_ring_daily` (
  `account_id` VARBINARY(63) NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `day_ymd` INT UNSIGNED NOT NULL,
  `ring_count` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `bonus_claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `task_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `target_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `target_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `progress` INT UNSIGNED NOT NULL DEFAULT 0,
  `target_npc` VARBINARY(16) NOT NULL DEFAULT '',
  `task_active` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `day_ymd`)
) ENGINE=InnoDB;
