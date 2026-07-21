USE `jh_online`;

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

-- 将通讯密钥写入数据库（不要把真实密钥提交到脚本）：
-- UPDATE server_payment_config SET secret_key='你的通讯密钥' WHERE config_id=1;
-- 如支付平台后台没有设置通知地址，还应设置本站公开地址（不要以 / 结尾）：
-- UPDATE server_payment_config SET callback_base_url='https://你的账号中心域名' WHERE config_id=1;
