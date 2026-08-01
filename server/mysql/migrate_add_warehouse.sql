USE `jh_online`;

-- Player warehouse slots, separate from account_role_backpack.
-- Opened via mall durable pass item 834 through the proven 26/1 NPC-service
-- dialog path (retrieve/deposit menus), not the unresolved native 钱庄 WT.
CREATE TABLE IF NOT EXISTS `account_role_warehouse` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `slot_index` SMALLINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_seq` SMALLINT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`, `role_id`, `slot_index`),
  KEY `idx_account_role_warehouse_item` (`account_id`, `role_id`, `item_id`, `item_seq`),
  CONSTRAINT `fk_account_role_warehouse_role`
    FOREIGN KEY (`account_id`, `role_id`)
    REFERENCES `account_roles` (`account_id`, `role_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;
