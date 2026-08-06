USE `jh_online`;

/* MySQL 5.x-compatible companion for existing server_shop_items tables.
 * It is idempotent so applying schema.sql and all migrations together is
 * safe. The server also probes information_schema and applies the same ALTER
 * automatically. */
SET @shop_section_sql = IF(
  (SELECT COUNT(*) FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'server_shop_items'
     AND COLUMN_NAME = 'shop_section') = 0,
  'ALTER TABLE `server_shop_items` ADD COLUMN `shop_section` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `enabled`',
  'SELECT 1'
);
PREPARE shop_section_stmt FROM @shop_section_sql;
EXECUTE shop_section_stmt;
DEALLOCATE PREPARE shop_section_stmt;
