USE `jh_online`;

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

-- 解锁后台：
-- UPDATE server_admin_config
-- SET failed_attempts = 0, locked = 0
-- WHERE config_id = 1;

-- 修改后台密码（示例）：
-- UPDATE server_admin_config
-- SET password_value = 'new-password', failed_attempts = 0, locked = 0
-- WHERE config_id = 1;
