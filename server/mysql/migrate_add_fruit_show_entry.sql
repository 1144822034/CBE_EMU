-- 水果机：娱乐入口显示开关（与 enabled 全面关闭分离）
-- 列已存在时可忽略 Duplicate column 错误
ALTER TABLE `server_fruit_config`
  ADD COLUMN `show_entry` TINYINT UNSIGNED NOT NULL DEFAULT 1
  COMMENT '0=娱乐列表隐藏' AFTER `enabled`;

UPDATE `server_fruit_config` SET `show_entry` = 0 WHERE `enabled` = 0;
