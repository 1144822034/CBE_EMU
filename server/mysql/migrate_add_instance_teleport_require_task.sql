USE `jh_online`;

-- Optional quest gate for instance-guide 传送 root dialog option.
-- 0 = always show teleport when target_scene is set; else require state=3.
ALTER TABLE `server_dynamic_npc_instances`
  ADD COLUMN `teleport_require_task_id` INT UNSIGNED NOT NULL DEFAULT 0
  COMMENT '0=always show teleport; else show after this task completed(state3)'
  AFTER `minimum_level`;
