USE `jh_online`;

-- Special-title conditions introduced before the equipment contract used a
-- temporary level-1 placeholder.  These two rows are now fixed gameplay
-- rules and may not be converted into a global money/level unlock.
UPDATE `server_role_designations`
SET `condition_kind` = 3, `condition_value` = 1
WHERE `designation_id` = 33
  AND (`condition_kind` <> 3 OR `condition_value` <> 1);

UPDATE `server_role_designations`
SET `condition_kind` = 3, `condition_value` = 2
WHERE `designation_id` = 34
  AND (`condition_kind` <> 3 OR `condition_value` <> 2);
