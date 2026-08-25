-- 比武擂台 (Pixi arena) + 擂台赛押金预留
-- Runtime also CREATE IF NOT EXISTS via mock_server_arena.c; this file is for ops.

CREATE TABLE IF NOT EXISTS `server_arena_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `scene` VARBINARY(64) NOT NULL,
  `spawn_x` SMALLINT UNSIGNED NOT NULL DEFAULT 120,
  `spawn_y` SMALLINT UNSIGNED NOT NULL DEFAULT 120,
  `weekday_mask` INT UNSIGNED NOT NULL DEFAULT 126 COMMENT 'bit0=Sun..bit6=Sat; derived from weekday_brackets',
  `weekday_brackets` VARCHAR(32) NOT NULL DEFAULT '0,70,50,10,70,50,10' COMMENT 'Sun..Sat: 0=rest,10/50/70',
  `entry_start_minute` SMALLINT UNSIGNED NOT NULL DEFAULT 1220,
  `entry_end_minute` SMALLINT UNSIGNED NOT NULL DEFAULT 1229,
  `match_start_minute` SMALLINT UNSIGNED NOT NULL DEFAULT 1230,
  `match_end_minute` SMALLINT UNSIGNED NOT NULL DEFAULT 1260,
  `round_seconds` INT UNSIGNED NOT NULL DEFAULT 300,
  `battle_timeout_seconds` INT UNSIGNED NOT NULL DEFAULT 240,
  `score_win` INT UNSIGNED NOT NULL DEFAULT 20,
  `score_bye` INT UNSIGNED NOT NULL DEFAULT 20,
  `score_draw` INT UNSIGNED NOT NULL DEFAULT 10,
  `npc_name_keyword` VARBINARY(32) NOT NULL DEFAULT '',
  `wager_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `wager_fee_percent` TINYINT UNSIGNED NOT NULL DEFAULT 10,
  `wager_currency` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=铜币',
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_rewards` (
  `job` TINYINT UNSIGNED NOT NULL,
  `bracket` SMALLINT UNSIGNED NOT NULL,
  `rank_pos` TINYINT UNSIGNED NOT NULL,
  `reward_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_wcoin` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `equip_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`job`, `bracket`, `rank_pos`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_sessions` (
  `session_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `day_ymd` INT UNSIGNED NOT NULL,
  `slot` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=entry 1=live 2=settled',
  `entry_start_ts` INT UNSIGNED NOT NULL DEFAULT 0,
  `match_start_ts` INT UNSIGNED NOT NULL DEFAULT 0,
  `match_end_ts` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`session_id`),
  UNIQUE KEY `uk_arena_sessions_day_slot` (`day_ymd`, `slot`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_scores` (
  `session_id` INT UNSIGNED NOT NULL,
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_name` VARBINARY(32) NOT NULL,
  `job` TINYINT UNSIGNED NOT NULL,
  `bracket` SMALLINT UNSIGNED NOT NULL,
  `level` INT UNSIGNED NOT NULL,
  `score` INT UNSIGNED NOT NULL DEFAULT 0,
  `wins` INT UNSIGNED NOT NULL DEFAULT 0,
  `losses` INT UNSIGNED NOT NULL DEFAULT 0,
  `draws` INT UNSIGNED NOT NULL DEFAULT 0,
  `byes` INT UNSIGNED NOT NULL DEFAULT 0,
  `rank_pos` INT UNSIGNED NOT NULL DEFAULT 0,
  `claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`session_id`, `role_id`),
  KEY `idx_arena_scores_bucket` (`session_id`, `job`, `bracket`, `score`),
  CONSTRAINT `fk_arena_scores_session`
    FOREIGN KEY (`session_id`) REFERENCES `server_arena_sessions` (`session_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_titles` (
  `job` TINYINT UNSIGNED NOT NULL,
  `bracket` SMALLINT UNSIGNED NOT NULL,
  `place` TINYINT UNSIGNED NOT NULL COMMENT '1冠军 2亚军 3季军',
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_name` VARBINARY(32) NOT NULL,
  `session_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`job`, `bracket`, `place`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_arena_wager_challenges` (
  `challenge_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `host_role_id` INT UNSIGNED NOT NULL,
  `host_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `challenger_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `challenger_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `scene` VARBINARY(64) NOT NULL,
  `stake_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `fee_percent` TINYINT UNSIGNED NOT NULL DEFAULT 10,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=open 1=matched 2=done 3=cancel',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`challenge_id`),
  KEY `idx_arena_wager_host` (`host_role_id`, `status`)
) ENGINE=InnoDB;

-- 赏金擂台成员与人数列
ALTER TABLE `server_arena_wager_challenges`
  ADD COLUMN `host_count` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  ADD COLUMN `challenger_count` TINYINT UNSIGNED NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS `server_arena_wager_members` (
  `challenge_id` INT UNSIGNED NOT NULL,
  `side` TINYINT UNSIGNED NOT NULL COMMENT '0=host 1=challenger',
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `role_name` VARBINARY(32) NOT NULL DEFAULT '',
  `paid` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `accepted` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `payout` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`challenge_id`, `role_id`),
  KEY `idx_wager_member_role` (`role_id`)
) ENGINE=InnoDB;
