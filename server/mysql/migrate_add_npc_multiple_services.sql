USE `jh_online`;

-- Adds the multi-service relation used by the NPC editor.  Existing NPCs are
-- not copied here: absence of rows deliberately preserves their old single
-- `npc_kind` / native `service_kind` behavior until an administrator saves
-- that NPC with the new editor.
CREATE TABLE IF NOT EXISTS `server_npc_services` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `service_kind` SMALLINT UNSIGNED NOT NULL,
  `sort_order` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `option_name` VARBINARY(64) NOT NULL DEFAULT '',
  `option_description` VARBINARY(96) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`, `service_kind`),
  KEY `idx_server_npc_services_dialog` (`scene`, `actor_id`, `sort_order`, `service_kind`)
) ENGINE=InnoDB;
