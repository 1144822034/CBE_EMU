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

CREATE TABLE IF NOT EXISTS `server_registration_config` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `allow_game_auto_account` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_registration_config`
  (`config_id`, `allow_game_auto_account`)
VALUES
  (1, 1);

CREATE TABLE IF NOT EXISTS `server_smtp_config` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `host` VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `port` SMALLINT UNSIGNED NOT NULL DEFAULT 25,
  `username` VARBINARY(255) NOT NULL,
  `password_value` VARBINARY(255) NOT NULL,
  `sender_email` VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_smtp_config`
  (`config_id`, `host`, `port`, `username`, `password_value`, `sender_email`, `enabled`)
VALUES
  (1, '', 25, '', '', '', 0);

-- The server initializes the first row with the built-in localized template
-- on startup.  `{{code}}` in the body is substituted only at send time.
CREATE TABLE IF NOT EXISTS `server_registration_email_templates` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `subject` VARBINARY(255) NOT NULL,
  `body` VARBINARY(4096) NOT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_email_bindings` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `email` VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `verified_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`),
  UNIQUE KEY `uk_account_email_bindings_email` (`email`),
  CONSTRAINT `fk_account_email_bindings_account`
    FOREIGN KEY (`account_id`) REFERENCES `accounts` (`account_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `web_registration_email_verifications` (
  `email` VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `code_digest` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `attempt_count` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `expires_at` TIMESTAMP NOT NULL,
  `last_sent_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`email`),
  KEY `idx_web_registration_verifications_expires` (`expires_at`)
) ENGINE=InnoDB;

-- A short-lived, single-use image challenge is required before an email
-- verification code may be sent.  Only the answer digest is persisted.
CREATE TABLE IF NOT EXISTS `web_registration_image_captchas` (
  `captcha_token` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `email` VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `code_digest` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `attempt_count` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `expires_at` TIMESTAMP NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`captcha_token`),
  KEY `idx_web_registration_image_captchas_expires` (`expires_at`)
) ENGINE=InnoDB;

-- 独立于玩家 accounts 的后台操作员身份。首个操作员必须由可信数据库管理终端
-- 显式创建；服务不会再生成默认 admin 账号。
CREATE TABLE IF NOT EXISTS `server_admin_users` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `password_value` VARBINARY(64) NOT NULL,
  `failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `locked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;

-- 游戏、账号中心和后台管理共用的来源 IPv4 登录失败计数。达到 15 次后
-- 服务直接静默关闭该 IP 的后续连接，不再发送任何协议或 HTTP 响应。
CREATE TABLE IF NOT EXISTS `server_login_ip_blocks` (
  `ip_address` VARCHAR(45) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked_at` TIMESTAMP NULL DEFAULT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`ip_address`),
  KEY `idx_login_ip_blocks_blocked` (`blocked`)
) ENGINE=InnoDB;

-- 后台管理入口专用的来源 IPv4 登录风险。它与上面的 15 次共享封锁
-- 分开：后台连续失败 5 次只会拒绝后台登录，不会阻断同一 IP 的游戏或账号中心登录。
CREATE TABLE IF NOT EXISTS `server_admin_login_ip_blocks` (
  `ip_address` VARCHAR(15) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `last_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `failed_attempts` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `blocked_at` TIMESTAMP NULL DEFAULT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`ip_address`),
  KEY `idx_admin_login_ip_blocks_blocked` (`blocked`)
) ENGINE=InnoDB;

-- 网页反向代理的可信 TCP 来源。只有来源 IP 匹配且启用时，服务端才会
-- 采信对应的真实客户端 IP 请求头；127.0.0.1 是同机 nginx 的安全默认项。
CREATE TABLE IF NOT EXISTS `server_trusted_proxy_sources` (
  `source_ip` VARCHAR(15) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `trust_x_real_ip` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `trust_x_forwarded_for` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`source_ip`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_trusted_proxy_sources`
  (`source_ip`, `trust_x_real_ip`, `trust_x_forwarded_for`, `enabled`)
VALUES
  ('127.0.0.1', 1, 1, 1);

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

CREATE TABLE IF NOT EXISTS `server_content_update_releases` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `release_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `manifest_code` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
    ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_content_update_files` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `sort_order` SMALLINT UNSIGNED NOT NULL,
  `resource_name` VARBINARY(127) NOT NULL,
  `resource_checksum` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`config_id`, `sort_order`),
  UNIQUE KEY `uq_content_update_resource` (`config_id`, `resource_name`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_content_update_releases`
  (`config_id`, `enabled`, `release_id`, `manifest_code`)
VALUES (1, 0, 0, 0);

CREATE TABLE IF NOT EXISTS `world_chat_messages` (
  `message_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `source_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `source_role_id` INT UNSIGNED NOT NULL,
  `source_name` VARBINARY(15) NOT NULL,
  `message` VARBINARY(79) NOT NULL,
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

-- `account_role_state.role_count` is a derived value.  The service installs
-- the enforcing triggers at startup so existing deployments can fail closed
-- when a trigger is missing or has been replaced.  This table keeps the
-- attempted value and connection identity when a direct state write is
-- corrected by those triggers.
CREATE TABLE IF NOT EXISTS `account_role_count_write_audit` (
  `audit_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `operation` VARCHAR(16) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `attempted_role_count` INT UNSIGNED NOT NULL,
  `authoritative_role_count` INT UNSIGNED NOT NULL,
  `connection_id` BIGINT UNSIGNED NOT NULL,
  `database_user` VARCHAR(288) CHARACTER SET utf8mb4 NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`audit_id`),
  KEY `idx_role_count_write_audit_account` (`account_id`, `created_at`)
) ENGINE=InnoDB;

-- 后台对账号和角色执行的人工管理操作，以及游戏内已提交的 W 币消费。
-- 该表没有角色外键，保证角色删除、迁移或账号状态变化后仍可保留操作当时的
-- 目标身份与审计证据。
CREATE TABLE IF NOT EXISTS `server_admin_operation_logs` (
  `log_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `operator_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `action_code` VARCHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `target_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `target_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `change_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `detail` VARBINARY(255) NOT NULL,
  `created_at` TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`log_id`),
  KEY `idx_admin_operation_logs_account` (`target_account_id`, `log_id`),
  KEY `idx_admin_operation_logs_created` (`created_at`, `log_id`)
) ENGINE=InnoDB;

-- 称号名称和资源编号由客户端已验证目录固定。condition_kind：1=持有铜钱，
-- 2=角色等级，3=固定的全套装备；后台只配置允许编辑的启用状态及达成门槛。
CREATE TABLE IF NOT EXISTS `server_role_designations` (
  `designation_id` TINYINT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `condition_kind` TINYINT UNSIGNED NOT NULL,
  `condition_value` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`designation_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_transfer_codes` (
  `verification_code` CHAR(8) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `source_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `expires_unix` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`verification_code`),
  UNIQUE KEY `uq_account_role_transfer_source_role` (`source_account_id`, `role_id`),
  KEY `idx_account_role_transfer_expiry` (`expires_unix`),
  CONSTRAINT `fk_account_role_transfer_source_role`
    FOREIGN KEY (`source_account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_equipment` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `slot_index` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_affix_types` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '四个强化阶段词条类型，每字节一个',
  `enhance_affix_values` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '四个强化阶段词条数值，每 16 位一个',
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
  `enhance_affix_types` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '四个强化阶段词条类型，每字节一个',
  `enhance_affix_values` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '四个强化阶段词条数值，每 16 位一个',
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
  `repeatable` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=不可重复,1=不限次数,2=每日一次,3=每周一次,4=每月一次',
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
  `spawn_enemy_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
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
-- 1..10 are parser-backed NPC-dialog service kinds (guard challenge uses
-- client-native action=13).  Rows are
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
  `drop_rate_percent` DECIMAL(5,2) UNSIGNED NOT NULL DEFAULT 0,
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
  `drop_rate_percent` DECIMAL(5,2) UNSIGNED NOT NULL,
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

CREATE TABLE IF NOT EXISTS `server_global_reward_mails` (
  `mail_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `title` VARBINARY(63) NOT NULL,
  `body` VARBINARY(255) NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=草稿,1=已发放,2=已撤回',
  `recipient_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `sent_at` TIMESTAMP NULL DEFAULT NULL,
  `revoked_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`mail_id`),
  KEY `idx_global_reward_mails_status` (`status`, `mail_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_global_reward_mail_items` (
  `mail_id` INT UNSIGNED NOT NULL,
  `reward_order` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`mail_id`, `reward_order`),
  KEY `idx_global_reward_mail_items_item` (`item_id`),
  CONSTRAINT `fk_global_reward_mail_items_mail`
    FOREIGN KEY (`mail_id`) REFERENCES `server_global_reward_mails` (`mail_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_reward_mails` (
  `mail_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `claim_state` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=未领取,1=已领取,2=已撤回',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `claimed_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`mail_id`, `account_id`),
  KEY `idx_account_reward_mails_inbox` (`account_id`, `claim_state`, `mail_id`),
  CONSTRAINT `fk_account_reward_mails_mail`
    FOREIGN KEY (`mail_id`) REFERENCES `server_global_reward_mails` (`mail_id`)
    ON DELETE CASCADE,
  CONSTRAINT `fk_account_reward_mails_account`
    FOREIGN KEY (`account_id`) REFERENCES `accounts` (`account_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

-- 管理后台从历史 SQL 备份覆盖装备强化字段时写入的审计记录。
-- 恢复器只更新三个强化列，绝不执行上传 SQL 的其他语句。
CREATE TABLE IF NOT EXISTS `server_admin_enhancement_restore_audit` (
  `audit_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `source_name` VARCHAR(127) NOT NULL,
  `source_row_count` INT UNSIGNED NOT NULL,
  `matched_row_count` INT UNSIGNED NOT NULL,
  `changed_row_count` INT UNSIGNED NOT NULL,
  `missing_row_count` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`audit_id`)
) ENGINE=InnoDB;
