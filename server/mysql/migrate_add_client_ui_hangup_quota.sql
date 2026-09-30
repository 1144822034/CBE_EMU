-- Pixi: without 战斗心得, hangup battle quota (Client UI admin tab).
-- Runtime also ADD COLUMN via mock_server_client_ui_config.inc.c ensure_table.
-- 1 = cap at 64 fights without insight (default); 0 = unlimited without insight.
ALTER TABLE `server_client_ui_config`
  ADD COLUMN `hangup_quota_without_insight` TINYINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT '1=cap hangup at 64 without 战斗心得; 0=unlimited'
  AFTER `multi_open_limit`;
