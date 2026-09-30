-- 商品货架：商城（enabled）与 NPC 药商（npc_enabled）独立开关。
-- 服务启动时也会 ALTER + 一次性 backfill（migration shop-npc-enabled-v1）。
USE `jh_online`;

ALTER TABLE `server_shop_items`
  ADD COLUMN `npc_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `enabled`;

-- 旧语义：enabled=0 的药品走药商；迁移为显式 npc_enabled=1。
UPDATE `server_shop_items` SET `npc_enabled` = 1 WHERE `enabled` = 0;

INSERT IGNORE INTO `server_data_migrations`(`migration_name`)
VALUES ('shop-npc-enabled-v1');
