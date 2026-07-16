USE `jh_online`;

-- 执行前必须停止 mock-service。原表完整保留为只读迁移备份。
RENAME TABLE `account_role_state` TO `account_role_state_payload_backup`;

CREATE TABLE `account_role_state` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `format_version` INT UNSIGNED NOT NULL,
  `active_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `role_count` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`)
) ENGINE=InnoDB;

CREATE TABLE `account_roles` (
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

CREATE TABLE `account_role_equipment` (
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

CREATE TABLE `account_role_backpack` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `slot_index` SMALLINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_seq` SMALLINT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`account_id`, `role_id`, `slot_index`),
  KEY `idx_account_role_backpack_item` (`account_id`, `role_id`, `item_id`, `item_seq`),
  CONSTRAINT `fk_account_role_backpack_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE `role_id_sequence` (
  `role_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`role_id`)
) ENGINE=InnoDB AUTO_INCREMENT=10001;
