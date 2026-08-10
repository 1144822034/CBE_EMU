USE `jh_online`;

CREATE TABLE IF NOT EXISTS `server_dynamic_npcs` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `pos_x` SMALLINT UNSIGNED NOT NULL,
  `pos_y` SMALLINT UNSIGNED NOT NULL,
  `npc_kind` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `orientation` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `actor_resource` VARBINARY(64) NOT NULL,
  `display_name` VARBINARY(32) NOT NULL,
  `script_name` VARBINARY(64) NOT NULL DEFAULT '',
  `service_option_name` VARBINARY(64) NOT NULL DEFAULT '',
  `service_option_description` VARBINARY(96) NOT NULL DEFAULT '',
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS `server_dynamic_npc_instances` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `target_scene` VARBINARY(64) NOT NULL DEFAULT '',
  `target_x` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `target_y` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `challenge_enemy_id` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `minimum_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`),
  CONSTRAINT `fk_server_dynamic_npc_instances_npc`
    FOREIGN KEY (`scene`, `actor_id`)
    REFERENCES `server_dynamic_npcs` (`scene`, `actor_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;
