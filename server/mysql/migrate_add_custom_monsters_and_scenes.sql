USE `jh_online`;

-- Custom monster catalog entries created from the admin UI.
-- Runtime also CREATE TABLE IF NOT EXISTS these on load.
CREATE TABLE IF NOT EXISTS `server_monster_catalog_extra` (
  `monster_id` SMALLINT UNSIGNED NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  `family` TINYINT UNSIGNED NOT NULL,
  `display_name` VARBINARY(31) NOT NULL,
  `source_label` VARBINARY(63) NOT NULL,
  `actor_resource` VARBINARY(63) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`monster_id`)
) ENGINE=InnoDB;

-- Per-scene automonster.dsh three-slot overrides for hangup / scene battle picks.
CREATE TABLE IF NOT EXISTS `server_scene_monsters` (
  `scene` VARBINARY(63) NOT NULL,
  `monster_id_1` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `monster_id_2` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `monster_id_3` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`)
) ENGINE=InnoDB;
