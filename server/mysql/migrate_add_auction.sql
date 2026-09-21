-- 物品竞拍 (Pixi auction house) WT kind=42
-- Runtime also CREATE IF NOT EXISTS via mock_server_auction.c

CREATE TABLE IF NOT EXISTS `server_auction_config` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `fee_percent_base` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT '基础手续费%',
  `fee_percent_per_tier` TINYINT UNSIGNED NOT NULL DEFAULT 2 COMMENT '每档时长额外%',
  `min_bid_step` INT UNSIGNED NOT NULL DEFAULT 1,
  `currency_mask` TINYINT UNSIGNED NOT NULL DEFAULT 3 COMMENT 'bit0铜币 bit1酷宝',
  `max_active_listings` INT UNSIGNED NOT NULL DEFAULT 2,
  `category_gate` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1=按交易规则拦截上架',
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `server_auction_listings` (
  `listing_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `seller_role_id` INT UNSIGNED NOT NULL,
  `seller_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `seller_name` VARBINARY(32) NOT NULL DEFAULT '',
  `item_id` INT UNSIGNED NOT NULL,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 1,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `gem_reflect` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gem_combo` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gem_revive` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `start_price` INT UNSIGNED NOT NULL DEFAULT 0,
  `current_price` INT UNSIGNED NOT NULL DEFAULT 0,
  `buyout_price` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=无一口价',
  `currency` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0铜币 1酷宝',
  `fee_paid` INT UNSIGNED NOT NULL DEFAULT 0,
  `fee_percent` TINYINT UNSIGNED NOT NULL DEFAULT 2,
  `duration_tier` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0open 1sold 2expired 3cancelled',
  `high_bidder_role_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `high_bidder_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL DEFAULT '',
  `expire_unix` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`listing_id`),
  KEY `idx_auction_listings_status_expire` (`status`, `expire_unix`),
  KEY `idx_auction_listings_seller` (`seller_role_id`, `status`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_auction_bids` (
  `bid_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `listing_id` INT UNSIGNED NOT NULL,
  `bidder_role_id` INT UNSIGNED NOT NULL,
  `bidder_account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `currency` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0active 1outbid 2won 3refunded',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`bid_id`),
  KEY `idx_auction_bids_listing` (`listing_id`, `status`),
  KEY `idx_auction_bids_bidder` (`bidder_role_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_auction_claims` (
  `claim_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `role_id` INT UNSIGNED NOT NULL,
  `account_id` VARCHAR(63) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `kind` TINYINT UNSIGNED NOT NULL COMMENT '1item_return 2seller_payout 3bid_refund 4winner_item',
  `listing_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `item_instance_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `enhance_wash` VARCHAR(96) NOT NULL DEFAULT '0,0,0,0,0;0,0,0,0,0;0,0,0,0,0;0,0,0,0,0',
  `gem_reflect` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gem_combo` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `gem_revive` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `money_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `wcoin_amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`claim_id`),
  KEY `idx_auction_claims_role` (`role_id`, `claimed`)
) ENGINE=InnoDB;
