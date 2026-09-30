-- 藏宝阁：每人同时上架上限（可在管理后台配置）
-- mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_zangbao_max_active_listings.sql
-- 运行时 ensure_schema 也会 ADD COLUMN；本脚本供手工补齐旧库。

ALTER TABLE `server_kcoin_config`
  ADD COLUMN `max_active_listings` INT UNSIGNED NOT NULL DEFAULT 10 AFTER `enabled`;
