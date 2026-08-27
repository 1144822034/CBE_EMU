-- Per-day arena bracket schedule (one tier open each day).
-- Sun..Sat CSV: 0=rest, 10=10-49, 50=50-69, 70=70+.
-- Default: Mon70 Tue50 Wed10 Thu70 Fri50 Sat10 Sun rest.
-- Venues are by job (天机=base, 幻剑=base50, 鬼道=base70), not by level.
-- Safe to re-run only if column missing; duplicate column errors are OK to ignore.

ALTER TABLE `server_arena_config`
  ADD COLUMN `weekday_brackets` VARCHAR(32) NOT NULL
    DEFAULT '0,70,50,10,70,50,10'
    COMMENT 'Sun..Sat: 0=rest,10/50/70'
    AFTER `weekday_mask`;

UPDATE `server_arena_config`
SET `weekday_brackets` = '0,70,50,10,70,50,10',
    `weekday_mask` = 126
WHERE `id` = 1
  AND (`weekday_brackets` = '' OR `weekday_brackets` = '0,0,0,0,0,0,0');
