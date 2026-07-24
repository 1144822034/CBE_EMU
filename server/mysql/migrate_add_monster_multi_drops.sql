USE `jh_online`;

-- Run after migrate_add_monster_management.sql.  Existing one-item drop
-- overrides are copied into slot 1 exactly once; later backend saves use only
-- server_monster_drops and clear the legacy columns on their parent row.
CREATE TABLE IF NOT EXISTS `server_monster_drops` (
  `monster_id` SMALLINT UNSIGNED NOT NULL,
  `drop_slot` TINYINT UNSIGNED NOT NULL,
  `item_id` INT UNSIGNED NOT NULL,
  `drop_rate_percent` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`monster_id`, `drop_slot`),
  CONSTRAINT `fk_server_monster_drops_monster`
    FOREIGN KEY (`monster_id`) REFERENCES `server_monsters` (`monster_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;

INSERT IGNORE INTO `server_monster_drops` (
  `monster_id`, `drop_slot`, `item_id`, `drop_rate_percent`
)
SELECT `monster_id`, 1, `drop_item_id`, `drop_rate_percent`
FROM `server_monsters`
WHERE `drop_item_id` <> 0 AND `drop_rate_percent` <> 0;
