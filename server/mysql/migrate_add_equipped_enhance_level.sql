USE `jh_online`;

-- Worn gear previously only stored item_id.  Equip then unequip rebuilt the
-- backpack row with enhance_level=0, wiping successful 29/3 enhancements.
ALTER TABLE `account_role_equipment`
  ADD COLUMN `enhance_level` SMALLINT UNSIGNED NOT NULL DEFAULT 0
  AFTER `item_id`;
