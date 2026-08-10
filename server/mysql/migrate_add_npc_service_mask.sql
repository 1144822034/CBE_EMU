-- Multi-service dynamic NPCs: one NPC can offer several root action=1 entries.
-- Bit N (1..6) in npc_service_mask enables that npc_kind.  0 keeps legacy
-- single-kind behavior via npc_kind alone until backfill runs.

ALTER TABLE `server_dynamic_npcs`
  ADD COLUMN `npc_service_mask` INT UNSIGNED NOT NULL DEFAULT 0
  COMMENT 'bits 1..6 = enabled root services; 0=derive from npc_kind'
  AFTER `npc_kind`;

UPDATE `server_dynamic_npcs`
SET `npc_service_mask` = CASE
  WHEN `npc_kind` BETWEEN 1 AND 6 THEN (1 << `npc_kind`)
  ELSE 0
END
WHERE `npc_service_mask` = 0 AND `npc_kind` BETWEEN 1 AND 6;
