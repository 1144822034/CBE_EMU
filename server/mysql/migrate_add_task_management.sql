USE `jh_online`;

-- 服务端任务定义。task.dsh 中的原始任务无需复制到此表；只有后台编辑过的
-- 覆盖记录和新增任务写入这里。
CREATE TABLE IF NOT EXISTS `server_tasks` (
  `task_id` INT UNSIGNED NOT NULL,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `level` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `difficulty` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `classification` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `requirement_type1` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=无,1=物品,2=怪物',
  `requirement_count1` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `requirement_id1` INT UNSIGNED NOT NULL DEFAULT 0,
  `requirement_type2` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=无,1=物品,2=怪物',
  `requirement_count2` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `requirement_id2` INT UNSIGNED NOT NULL DEFAULT 0,
  `prerequisite_task_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `given_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `given_item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_exp` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_money` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_count` INT UNSIGNED NOT NULL DEFAULT 0,
  `reward_item_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `name` VARBINARY(31) NOT NULL,
  `giver` VARBINARY(15) NOT NULL,
  `receiver` VARBINARY(15) NOT NULL,
  `goal` VARBINARY(95) NOT NULL DEFAULT '',
  `reward_text` VARBINARY(31) NOT NULL DEFAULT '',
  `offer_dialog` VARBINARY(255) NOT NULL DEFAULT '',
  `active_dialog` VARBINARY(255) NOT NULL DEFAULT '',
  `completed_dialog` VARBINARY(255) NOT NULL DEFAULT '',
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`task_id`),
  KEY `idx_server_tasks_enabled` (`enabled`, `task_id`)
) ENGINE=InnoDB;

-- 动态 NPC 与一个可接取任务之间的一对一绑定。任务本身可以被多个 NPC 使用。
CREATE TABLE IF NOT EXISTS `server_dynamic_npc_tasks` (
  `scene` VARBINARY(64) NOT NULL,
  `actor_id` INT UNSIGNED NOT NULL,
  `task_id` INT UNSIGNED NOT NULL,
  `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`scene`, `actor_id`),
  KEY `idx_server_dynamic_npc_tasks_task` (`task_id`),
  CONSTRAINT `fk_server_dynamic_npc_tasks_npc`
    FOREIGN KEY (`scene`, `actor_id`)
    REFERENCES `server_dynamic_npcs` (`scene`, `actor_id`)
    ON DELETE CASCADE
) ENGINE=InnoDB;
