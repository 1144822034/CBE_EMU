-- Support two decimal places for monster drop probabilities.  Existing whole
-- percentages keep their numeric meaning (for example 5 becomes 5.00).
ALTER TABLE `server_monsters`
  MODIFY COLUMN `drop_rate_percent` DECIMAL(5,2) UNSIGNED NOT NULL DEFAULT 0;

ALTER TABLE `server_monster_drops`
  MODIFY COLUMN `drop_rate_percent` DECIMAL(5,2) UNSIGNED NOT NULL;
