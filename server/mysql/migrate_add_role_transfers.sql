USE `jh_online`;

CREATE TABLE IF NOT EXISTS `server_data_migrations` (
  `migration_name` VARCHAR(127) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`migration_name`)
) ENGINE=InnoDB;

START TRANSACTION;

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

INSERT IGNORE INTO `server_data_migrations` (`migration_name`)
VALUES ('2026-08-14-role-transfer-codes');

COMMIT;
