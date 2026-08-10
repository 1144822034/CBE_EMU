USE `jh_online`;

-- Allow multiple accept bindings per dynamic NPC.
-- Old PK was (scene, actor_id); new PK is (scene, actor_id, task_id).
-- Runtime also applies this via information_schema check on load.

ALTER TABLE `server_dynamic_npc_tasks`
  DROP FOREIGN KEY `fk_server_dynamic_npc_tasks_npc`;

ALTER TABLE `server_dynamic_npc_tasks`
  DROP PRIMARY KEY,
  ADD PRIMARY KEY (`scene`, `actor_id`, `task_id`);

ALTER TABLE `server_dynamic_npc_tasks`
  ADD CONSTRAINT `fk_server_dynamic_npc_tasks_npc`
    FOREIGN KEY (`scene`, `actor_id`)
    REFERENCES `server_dynamic_npcs` (`scene`, `actor_id`)
    ON DELETE CASCADE;
