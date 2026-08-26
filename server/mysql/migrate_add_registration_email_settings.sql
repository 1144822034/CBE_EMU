USE `jh_online`;

-- Registration policy is independent of the browser process.  The game
-- transport reads this row before it allocates a guest account.
CREATE TABLE IF NOT EXISTS `server_registration_config` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `allow_game_auto_account` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_registration_config`
  (`config_id`, `allow_game_auto_account`)
VALUES
  (1, 1);

-- The service intentionally stores no SMTP password in operation logs or
-- rendered HTML.  Configure an internal SMTP relay for production deployments
-- when this build is used without a TLS-capable transport dependency.
CREATE TABLE IF NOT EXISTS `server_smtp_config` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `host` VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `port` SMALLINT UNSIGNED NOT NULL DEFAULT 25,
  `username` VARBINARY(255) NOT NULL,
  `password_value` VARBINARY(255) NOT NULL,
  `sender_email` VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_smtp_config`
  (`config_id`, `host`, `port`, `username`, `password_value`, `sender_email`, `enabled`)
VALUES
  (1, '', 25, '', '', '', 0);

-- The service inserts its localized default on startup when this table has no
-- row.  The body must retain `{{code}}`, which is replaced at send time.
CREATE TABLE IF NOT EXISTS `server_registration_email_templates` (
  `config_id` TINYINT UNSIGNED NOT NULL,
  `subject` VARBINARY(255) NOT NULL,
  `body` VARBINARY(4096) NOT NULL,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_id`)
) ENGINE=InnoDB;

-- A verified email can belong to at most one player account.  Administrator-
-- provisioned and existing accounts remain valid without an email binding.
CREATE TABLE IF NOT EXISTS `account_email_bindings` (
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `email` VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `verified_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`account_id`),
  UNIQUE KEY `uk_account_email_bindings_email` (`email`),
  CONSTRAINT `fk_account_email_bindings_account`
    FOREIGN KEY (`account_id`) REFERENCES `accounts` (`account_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

-- Only a digest of the short-lived registration code is stored.  A successful
-- registration consumes the row in the same transaction that binds the email.
CREATE TABLE IF NOT EXISTS `web_registration_email_verifications` (
  `email` VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `code_digest` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `attempt_count` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `expires_at` TIMESTAMP NOT NULL,
  `last_sent_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`email`),
  KEY `idx_web_registration_verifications_expires` (`expires_at`)
) ENGINE=InnoDB;

-- The browser receives the visual image, while the database keeps only a
-- short-lived digest of its answer.  Successful validation consumes the row
-- before the SMTP email code is sent.
CREATE TABLE IF NOT EXISTS `web_registration_image_captchas` (
  `captcha_token` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `email` VARCHAR(254) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `code_digest` CHAR(32) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `attempt_count` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `expires_at` TIMESTAMP NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`captcha_token`),
  KEY `idx_web_registration_image_captchas_expires` (`expires_at`)
) ENGINE=InnoDB;
