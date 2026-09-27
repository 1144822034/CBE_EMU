-- Gate +16 worn gem embed (29/17). Default ON.
-- Runtime also probes information_schema and ALTERs if missing.
ALTER TABLE `server_gem_config`
  ADD COLUMN `socket_embed_enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1
  COMMENT '1=allow equip-bar 镶嵌 / 29/17; 0=hide + reject'
  AFTER `crystal16_exchange_enabled`;
