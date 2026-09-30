-- 商品价格：商城酷宝（price）与 NPC 铜币（npc_price）独立。
-- 服务启动时也会 ALTER + 一次性 backfill（migration shop-npc-price-v1）。
USE `jh_online`;

ALTER TABLE `server_shop_items`
  ADD COLUMN `npc_price` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `price`;

-- 旧语义：两通道共用 price；迁移为显式 npc_price=price。
UPDATE `server_shop_items` SET `npc_price` = `price` WHERE `npc_price` = 0;

INSERT IGNORE INTO `server_data_migrations`(`migration_name`)
VALUES ('shop-npc-price-v1');
