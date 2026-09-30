-- 角色封印卷：空白卷封印角色到托管账号；绑定卷可上架藏宝阁并过户。
-- mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_role_seal.sql

INSERT IGNORE INTO `accounts` (`account_id`, `password_value`)
VALUES ('__seal_escrow__', '!');

INSERT IGNORE INTO `account_role_state`
  (`account_id`, `format_version`, `active_role_id`, `role_count`)
VALUES ('__seal_escrow__', 1, 0, 0);

CREATE TABLE IF NOT EXISTS `server_sealed_roles` (
  `item_instance_id` INT UNSIGNED NOT NULL,
  `sealed_role_id` INT UNSIGNED NOT NULL,
  `origin_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `preview_name` VARBINARY(32) NOT NULL DEFAULT '',
  `preview_level` INT UNSIGNED NOT NULL DEFAULT 1,
  `preview_job` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `preview_sex` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `sealed_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`item_instance_id`),
  UNIQUE KEY `uk_sealed_roles_role` (`sealed_role_id`),
  KEY `idx_sealed_roles_origin` (`origin_account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
