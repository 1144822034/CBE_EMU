-- 藏宝阁 + 酷币：独立货币、寄售、赞助成交、提现审核
-- mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_kcoin_zangbao.sql

-- Idempotent at runtime via ensure_column; plain ALTER for older MySQL:
-- ALTER TABLE `account_roles` ADD COLUMN `kcoin` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `wcoin`;

CREATE TABLE IF NOT EXISTS `server_kcoin_config` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `kcoin_per_yuan` INT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `max_active_listings` INT UNSIGNED NOT NULL DEFAULT 10,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

-- Existing installs: ADD COLUMN is also applied at runtime by ensure_schema.
-- ALTER TABLE `server_kcoin_config` ADD COLUMN `max_active_listings` INT UNSIGNED NOT NULL DEFAULT 10 AFTER `enabled`;

INSERT IGNORE INTO `server_kcoin_config` (`config_id`, `kcoin_per_yuan`, `enabled`, `max_active_listings`)
VALUES (1, 1, 1, 10);

CREATE TABLE IF NOT EXISTS `server_zangbao_listings` (
  `listing_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `seller_role_id` INT UNSIGNED NOT NULL,
  `seller_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `seller_name` VARBINARY(32) NOT NULL DEFAULT '',
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `gem_reflect` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gem_combo` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gem_revive` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `price_kcoin` INT UNSIGNED NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=active 1=reserved 2=sold 3=cancelled',
  `buyer_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `buyer_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `reserve_expire_unix` INT UNSIGNED NOT NULL DEFAULT 0,
  `pay_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`listing_id`),
  KEY `idx_zangbao_listings_status` (`status`, `created_at`),
  KEY `idx_zangbao_listings_seller` (`seller_role_id`, `status`),
  KEY `idx_zangbao_listings_pay` (`pay_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `kcoin_market_orders` (
  `pay_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `provider_order_id` VARCHAR(127) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `seller_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `seller_role_id` INT UNSIGNED NOT NULL,
  `listing_id` INT UNSIGNED NOT NULL,
  `pay_type` TINYINT UNSIGNED NOT NULL,
  `price_cents` INT UNSIGNED NOT NULL,
  `really_price_cents` INT UNSIGNED NOT NULL DEFAULT 0,
  `price_kcoin` INT UNSIGNED NOT NULL,
  `seller_credit_kcoin` INT UNSIGNED NOT NULL,
  `fee_kcoin` INT UNSIGNED NOT NULL,
  `request_param` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `pay_url` VARBINARY(1024) NOT NULL DEFAULT '',
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `provider_state` INT NOT NULL DEFAULT 0,
  `timeout_minutes` SMALLINT UNSIGNED NOT NULL DEFAULT 5,
  `credited` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `paid_at` TIMESTAMP NULL DEFAULT NULL,
  `credited_at` TIMESTAMP NULL DEFAULT NULL,
  `last_checked_at` TIMESTAMP NULL DEFAULT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`pay_id`),
  UNIQUE KEY `uk_kcoin_market_provider_order` (`provider_order_id`),
  KEY `idx_kcoin_market_listing` (`listing_id`, `status`),
  KEY `idx_kcoin_market_buyer` (`account_id`, `created_at`),
  KEY `idx_kcoin_market_pending` (`status`, `created_at`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_zangbao_claims` (
  `claim_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `listing_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `gem_reflect` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gem_combo` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gem_revive` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`claim_id`),
  KEY `idx_zangbao_claims_role` (`role_id`, `claimed`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_role_kcoin_payee` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `pay_channel` TINYINT UNSIGNED NOT NULL COMMENT '1=微信 2=支付宝',
  `image_path` VARCHAR(255) NOT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`),
  CONSTRAINT `fk_kcoin_payee_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `kcoin_withdraw_orders` (
  `withdraw_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `amount` INT UNSIGNED NOT NULL,
  `pay_channel` TINYINT UNSIGNED NOT NULL,
  `qr_image_path` VARCHAR(255) NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=pending 1=approved 2=rejected',
  `admin_note` VARCHAR(255) NOT NULL DEFAULT '',
  `reviewed_at` TIMESTAMP NULL DEFAULT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`withdraw_id`),
  KEY `idx_kcoin_withdraw_status` (`status`, `created_at`),
  KEY `idx_kcoin_withdraw_role` (`role_id`, `created_at`)
) ENGINE=InnoDB;
