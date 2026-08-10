-- 场景战斗怪配置层
--
-- 保存的是部署草稿；服务端仅在后台显式“部署”时，才会把启用项编译为
-- 对应 SCE2 中的 kind-3 战斗记录。不要把本表直接用于动态 NPC 下行。

CREATE TABLE IF NOT EXISTS server_scene_battle_monsters (
    entry_id INT UNSIGNED NOT NULL AUTO_INCREMENT,
    scene VARBINARY(64) NOT NULL,
    monster_id SMALLINT UNSIGNED NOT NULL,
    pos_x SMALLINT UNSIGNED NOT NULL,
    pos_y SMALLINT UNSIGNED NOT NULL,
    display_name VARBINARY(30) NOT NULL,
    actor_resource VARBINARY(64) NOT NULL,
    -- Native SCE2 kind-3 field 18.  The client does not create a live
    -- battle node from a record that stops at actor_resource.
    effect_resource VARBINARY(64) NOT NULL DEFAULT 'e_ghostfireR.actor',
    visual_hint TINYINT UNSIGNED NOT NULL DEFAULT 5,
    enabled TINYINT UNSIGNED NOT NULL DEFAULT 1,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (entry_id),
    KEY idx_scene_battle_monster_scene (scene),
    UNIQUE KEY uq_scene_battle_monster_pos (scene, monster_id, pos_x, pos_y)
) ENGINE=InnoDB;

-- Existing installations are migrated automatically by the server before it
-- selects this field.  The default is an explicitly observed native scene
-- effect Actor, not a synthetic placeholder.

-- 保存首次部署前读取到的服务端 SCE 原始字节。后续部署始终由这份基础
-- 资源重建，避免在已部署输出上重复追加战斗节点。
CREATE TABLE IF NOT EXISTS server_scene_battle_monster_sources (
    scene VARBINARY(64) NOT NULL,
    base_resource MEDIUMBLOB NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (scene)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS server_scene_battle_monster_deployments (
    scene VARBINARY(64) NOT NULL,
    config_fingerprint INT UNSIGNED NOT NULL,
    configured_count SMALLINT UNSIGNED NOT NULL,
    deployed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (scene)
) ENGINE=InnoDB;
