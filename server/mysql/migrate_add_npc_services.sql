USE `jh_online`;

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
