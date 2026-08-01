USE `jh_online`;

-- Battle Actor key for monster admin catalog / overrides.
-- Prefer restarting the mock server: monster_db_load adds the column if missing.
-- Manual run only on installs that already have both tables and lack the column.

ALTER TABLE `server_monsters`
  ADD COLUMN `actor_resource` VARBINARY(63) NOT NULL DEFAULT ''
  AFTER `cast_skill`;

ALTER TABLE `server_monster_catalog_extra`
  ADD COLUMN `actor_resource` VARBINARY(63) NOT NULL DEFAULT ''
  AFTER `source_label`;
