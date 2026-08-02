# 铸剑谷小猴子「副本配置已失效」

## 症状

铸剑谷点小猴子 → 副本传送与挑战 / 挑战守关怪，对话提示：**副本配置已失效。**

## 提示含义

`mock_server_scene_sync.c` 服务对话在 `OPEN/ENTER/CHALLENGE_INSTANCE` 时调用
`vm_net_mock_instance_guide_seed(actorId)`；返回 `NULL` 即写该文案。

查找条件（缺一不可）：

1. `server_dynamic_npcs` 存在 `(scene, actor_id)` 覆盖行  
2. `npc_kind = 6`（副本向导）  
3. `enabled = 1`  
4. 场景键与查找场景 loose-equal  

内置小猴子 `20021`（`append_builtin_scene_npcinfo_seeds`）**不是**副本向导：无
`challenge_enemy_id` / `instance_scene`，`kind` 默认 0。只有后台把它改成副本向导
并填好扩展表后，挑战链路才成立。

## 根因（代码）

对话目录用 `vm_net_mock_client_visible_scene_name()`，而旧
`instance_guide_seed` 用 `vm_net_mock_current_scene_name()`（`role->scene`）。
多号恢复 / `c00` 与 `00` 别名时，对话能匹配到向导选项，点选后按
`role->scene` 查覆盖失败 → 误报配置失效。

## 修改

- `instance_guide_seed` 改用 visible scene（与 26/1 一致）
- 未命中时打 `mock_npc_instance_guide_miss`（no-override / not-instance-guide / disabled）

## 运营侧核对

后台「动态 NPC」→ 场景 `00蓬莱仙岛_02.sce`（或当前铸剑谷键）→ actor `20021`：

- 服务类型 = 副本向导  
- `challenge_enemy_id` 非 0 且怪物目录存在  
- 最低等级合理；仅挑战可空 `target_scene`  

## 验证

1. `make -j2 server`，重启。  
2. 铸剑谷小猴子挑战：应进 `30/9` 确认，不应再「配置已失效」。  
3. 若仍失败，日志应有 `mock_npc_instance_guide_miss` 标明具体 reason。  
