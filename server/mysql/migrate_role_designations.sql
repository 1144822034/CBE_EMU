USE `jh_online`;

-- 称号默认目录及特殊运营称号由服务首次读取时以 INSERT IGNORE 写入；
-- 本迁移只建立可持久化的达成条件配置表，不改动任何角色当前装备称号。
CREATE TABLE IF NOT EXISTS `server_role_designations` (
  `designation_id` TINYINT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `condition_kind` TINYINT UNSIGNED NOT NULL,
  `condition_value` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`designation_id`)
) ENGINE=InnoDB;
