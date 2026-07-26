USE `jh_online`;

CREATE TABLE IF NOT EXISTS `server_data_migrations` (
  `migration_name` VARCHAR(127) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`migration_name`)
) ENGINE=InnoDB;

START TRANSACTION;

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

INSERT IGNORE INTO `server_data_migrations` (`migration_name`)
VALUES ('2026-07-26-training-book-instances');

COMMIT;
