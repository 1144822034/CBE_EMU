-- 娱乐「猜猜看」(Pixi WT kind=45)
CREATE TABLE IF NOT EXISTS `server_guess_rounds` (
  `round_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `period_start_unix` INT UNSIGNED NOT NULL,
  `period_end_unix` INT UNSIGNED NOT NULL,
  `dice1` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `dice2` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `dice3` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`round_id`),
  UNIQUE KEY `idx_guess_rounds_period` (`period_start_unix`),
  KEY `idx_guess_rounds_end` (`status`, `period_end_unix`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_guess_bets` (
  `bet_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `round_id` INT UNSIGNED NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `choice` TINYINT UNSIGNED NOT NULL,
  `stake_copper` INT UNSIGNED NOT NULL,
  `payout_copper` INT UNSIGNED NOT NULL DEFAULT 0,
  `win` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`bet_id`),
  KEY `idx_guess_bets_role` (`role_id`, `bet_id` DESC),
  KEY `idx_guess_bets_round` (`round_id`, `win`, `payout_copper`)
) ENGINE=InnoDB;
