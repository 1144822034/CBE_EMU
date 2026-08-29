-- Permit one dynamic NPC to offer several task-hall actions.  Existing rows
-- are preserved; only the primary-key identity expands to include task_id.
ALTER TABLE `server_dynamic_npc_tasks`
  DROP PRIMARY KEY,
  ADD PRIMARY KEY (`scene`, `actor_id`, `task_id`);
