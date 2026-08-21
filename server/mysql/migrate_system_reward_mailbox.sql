CREATE TABLE IF NOT EXISTS `server_global_reward_mails` (
  `mail_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `title` VARBINARY(63) NOT NULL,
  `body` VARBINARY(255) NOT NULL,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `recipient_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `sent_at` TIMESTAMP NULL DEFAULT NULL,
  `revoked_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`mail_id`),
  KEY `idx_global_reward_mails_status` (`status`, `mail_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_global_reward_mail_items` (
  `mail_id` INT UNSIGNED NOT NULL,
  `reward_order` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`mail_id`, `reward_order`),
  KEY `idx_global_reward_mail_items_item` (`item_id`),
  CONSTRAINT `fk_global_reward_mail_items_mail`
    FOREIGN KEY (`mail_id`) REFERENCES `server_global_reward_mails` (`mail_id`) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `account_reward_mails` (
  `mail_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `claim_state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `claimed_at` TIMESTAMP NULL DEFAULT NULL,
  PRIMARY KEY (`mail_id`, `account_id`),
  KEY `idx_account_reward_mails_inbox` (`account_id`, `claim_state`, `mail_id`),
  CONSTRAINT `fk_account_reward_mails_mail`
    FOREIGN KEY (`mail_id`) REFERENCES `server_global_reward_mails` (`mail_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_account_reward_mails_account`
    FOREIGN KEY (`account_id`) REFERENCES `accounts` (`account_id`) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Upgrade deployments that already created one recipient row per role.  The
-- legacy table is intentionally retained as migration evidence.  A claimed
-- role wins over revoked/unclaimed rows because the reward has already entered
-- one role's backpack; revoked wins only when no role claimed the mail.
SET @mailbox_legacy_recipient_table_exists := (
  SELECT COUNT(*)
  FROM `information_schema`.`TABLES`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'account_role_reward_mails'
);

SET @mailbox_legacy_recipient_migration_sql := IF(
  @mailbox_legacy_recipient_table_exists = 1,
  'INSERT INTO account_reward_mails(mail_id,account_id,claim_state,created_at,claimed_at) SELECT arm.mail_id,arm.account_id,CASE WHEN MAX(arm.claim_state=1)>0 THEN 1 WHEN MAX(arm.claim_state=2)>0 THEN 2 ELSE 0 END,MIN(arm.created_at),MAX(CASE WHEN arm.claim_state=1 THEN arm.claimed_at ELSE NULL END) FROM account_role_reward_mails arm JOIN accounts a ON a.account_id=arm.account_id GROUP BY arm.mail_id,arm.account_id ON DUPLICATE KEY UPDATE created_at=LEAST(account_reward_mails.created_at,VALUES(created_at)),claimed_at=COALESCE(GREATEST(account_reward_mails.claimed_at,VALUES(claimed_at)),account_reward_mails.claimed_at,VALUES(claimed_at)),claim_state=CASE WHEN account_reward_mails.claim_state=1 OR VALUES(claim_state)=1 THEN 1 WHEN account_reward_mails.claim_state=2 OR VALUES(claim_state)=2 THEN 2 ELSE 0 END',
  'SELECT 1'
);

PREPARE mailbox_legacy_recipient_migration
  FROM @mailbox_legacy_recipient_migration_sql;
EXECUTE mailbox_legacy_recipient_migration;
DEALLOCATE PREPARE mailbox_legacy_recipient_migration;

UPDATE `server_global_reward_mails` AS `m`
SET `recipient_count` = (
  SELECT COUNT(*)
  FROM `account_reward_mails` AS `arm`
  WHERE `arm`.`mail_id` = `m`.`mail_id`
);
