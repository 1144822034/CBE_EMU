USE `jh_online`;

-- Run once for databases whose server_dynamic_npcs table predates
-- configurable display text for parser-backed NPC service options.
ALTER TABLE `server_dynamic_npcs`
  ADD COLUMN `service_option_name` VARBINARY(64) NOT NULL DEFAULT ''
    AFTER `script_name`,
  ADD COLUMN `service_option_description` VARBINARY(96) NOT NULL DEFAULT ''
    AFTER `service_option_name`;
