USE `jh_online`;

-- Repeatability belongs to the dynamic NPC-to-task binding. A completed
-- task may only be offered again by an NPC explicitly configured for it.
ALTER TABLE `server_dynamic_npc_tasks`
  ADD COLUMN `repeatable` TINYINT UNSIGNED NOT NULL DEFAULT 0
  COMMENT '0=不可重复,1=完成后可重新接取'
  AFTER `task_id`;
