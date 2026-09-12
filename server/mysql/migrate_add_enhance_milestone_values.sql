-- Default enhance milestone values by equip quality (Q0..Q4).
USE `jh_online`;

ALTER TABLE `server_enhance_milestones`
  ADD COLUMN `value_q0` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质0默认档数值' AFTER `enabled`,
  ADD COLUMN `value_q1` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质1默认档数值' AFTER `value_q0`,
  ADD COLUMN `value_q2` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质2默认档数值' AFTER `value_q1`,
  ADD COLUMN `value_q3` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质3默认档数值' AFTER `value_q2`,
  ADD COLUMN `value_q4` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '品质4默认档数值' AFTER `value_q3`;
