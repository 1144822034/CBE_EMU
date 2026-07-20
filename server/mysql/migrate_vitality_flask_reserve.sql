USE `jh_online`;

CREATE TABLE IF NOT EXISTS `server_data_migrations` (
  `migration_name` VARCHAR(127) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`migration_name`)
) ENGINE=InnoDB;

START TRANSACTION;

INSERT IGNORE INTO `server_data_migrations` (`migration_name`)
VALUES ('2026-07-20-vitality-flask-reserve');

SET @apply_vitality_flask_reserve = ROW_COUNT();

/*
 * The old mock stored these stack=1 items as ordinary unit counts.  The CBE
 * protocol instead uses the same u32 field as the remaining HP/MP reservoir.
 * Preserve multiple legacy units as their combined capacity in the existing
 * row; newly acquired flasks are emitted as separate stack=1 rows.
 */
UPDATE `account_role_backpack`
SET `item_count` = LEAST(CAST(`item_count` AS UNSIGNED) * 50000, 4294967295)
WHERE @apply_vitality_flask_reserve = 1
  AND `item_id` IN (802, 803)
  AND `item_count` > 0
  AND `item_count` < 50000;

COMMIT;
