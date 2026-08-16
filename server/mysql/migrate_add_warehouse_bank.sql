USE `jh_online`;

-- Warehouse gold bank for dialog-driven 仓库存取金币 (1金=10000铜).
-- Kept separate from account_role_warehouse item rows so item snapshot
-- DELETE/INSERT cannot wipe stored money.
CREATE TABLE IF NOT EXISTS `account_role_warehouse_bank` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `money` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`),
  CONSTRAINT `fk_account_role_warehouse_bank_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;
