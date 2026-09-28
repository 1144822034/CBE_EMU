-- Pixi client multi-open window limit (Client UI admin tab).
-- Runtime also ADD COLUMN via mock_server_client_ui_config.inc.c ensure_table.
ALTER TABLE `server_client_ui_config`
  ADD COLUMN `multi_open_limit` TINYINT UNSIGNED NOT NULL DEFAULT 1
    COMMENT 'Max concurrent Pixi windows on one device (1-8); 1=no multi-open'
  AFTER `mount_status_enabled`;
