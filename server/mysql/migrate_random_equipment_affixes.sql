USE `jh_online`;

-- Version 8 stores the +4/+8/+12/+16 roll on each concrete equipment
-- instance.  Zero is an intentional legacy marker: on the first service
-- load the authoritative normalizer rolls every already-unlocked stage once
-- and writes the result together with the rest of the role state.
ALTER TABLE `account_role_equipment`
  ADD COLUMN `enhance_affix_types` INT UNSIGNED NOT NULL DEFAULT 0
    AFTER `enhance_level`,
  ADD COLUMN `enhance_affix_values` BIGINT UNSIGNED NOT NULL DEFAULT 0
    AFTER `enhance_affix_types`;

ALTER TABLE `account_role_backpack`
  ADD COLUMN `enhance_affix_types` INT UNSIGNED NOT NULL DEFAULT 0
    AFTER `enhance_level`,
  ADD COLUMN `enhance_affix_values` BIGINT UNSIGNED NOT NULL DEFAULT 0
    AFTER `enhance_affix_types`;
