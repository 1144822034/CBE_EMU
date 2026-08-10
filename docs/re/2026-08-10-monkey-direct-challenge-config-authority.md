# 小猴子直接挑战：配置权威与旧场景发布排查

## 触发步骤

1. 进入 `00蓬莱仙岛_02.sce`。
2. 与 Actor `20021`（小猴子）对话并选择“挑战”。
3. 客户端先显示挑战入口，随后进入“请选择副本操作”；窗口没有任何选项。

## 首次偏离与证据

服务端原始日志显示首个 NPC 对话不是客户端原生的直接场景挑战：

```text
mock_npc_dialog actor=20021 ... service_mask=00000040 direct_challenge=0
mock_npc_service action=instance-menu ... options=0
```

数据库中的父动态 NPC 已配置为：

```text
scene=00蓬莱仙岛_02.sce
actor_id=20021
display_name=小猴子
actor_resource=e_monkey.actor
target_scene=""
challenge_enemy_id=1000
```

同时，`server_scene_battle_monsters` 中有同场景、启用的 `monster_id=1000`
草稿。旧逻辑却在加载动态 NPC 时调用全局
`vm_net_mock_monster_enemy_id_known(1000)`。该目录只收录可被当前 SCE
kind-3 解析器识别的记录，因此拒绝了父动态 NPC；随后服务选项表仍按
`(scene, actor_id)` 覆盖内置的小猴子，留下“有副本服务、无副本配置”的
半状态，最终构造出零项副本菜单。

检查当时服务器实际发布的 `web/fs/JHOnlineData/00蓬莱仙岛_02.sce` 后发现，
小猴子的 kind-3 记录只有 field 17（`e_monkey.actor`），缺少客户端场景加载器
要求的 field 18（特效 Actor）。因此该旧发布资源也不能创建可供
`mmBattle:0x66CC` 使用的 live type-2 节点。不是客户端可由 4/10 模板代替的
普通怪物战斗。

## 正确协议契约

- 无 `target_scene` 且配置了 `challenge_enemy_id` 的 NPC 服务，首个
  `26/1` 对话必须给出 action 13。
- 客户端随后从可见的 SCE kind-3 节点生成 `1/4/1`；服务端验证同场景、
  观察到的 live node index 后返回 `1/2/2 + 1/4/5`。
- `4/10` 是非场景模板战斗，不能替代这种场景挑战。

相关客户端证据：`task_hall_activate_selected_entry(0x010492B0)` action 13、
`SendNPCInteractReq(0x01037ED4)`、`mmBattle:HandleBattleStartMsg(0x66CC)`。

## 修复

1. 动态 NPC 的**直接场景挑战**改为精确查询同一场景的、启用的
   `server_scene_battle_monsters` 草稿，不再依赖全局怪物目录。
2. 仍保留目标场景副本对通用怪物目录的验证；两类战斗的权威来源不再混淆。
3. 场景战斗怪“已部署”状态由“部署表指纹相等”升级为“指纹相等且当前服务器
   SCE 中的每个启用 kind-3 记录可完整解析并精确匹配”。这会将旧的 field-17
   发布版本标记为未部署，避免假显示为可挑战。
4. 对旧发布版本不做登录时静默重写。管理员必须在“场景战斗怪”页面执行一次
   **部署场景战斗怪**，由既有的捕获基础资源 -> 生成完整 field-18 记录 -> WT18
   内容发布链路完成更新；这保持资源更新和客户端安装路径可审计。

### 回归补充：MySQL 回调不可重入

首次实现曾在 `vm_net_mock_dynamic_npc_row()` 的 `vm_mysql_query()` 结果回调中
调用“场景战斗怪是否存在”的查询。运行日志明确显示：

```text
[error][mysql] reentrant_query_denied phase=result-callback
dynamic_npc_instance_target_unresolved ... actor=20021 enemy=1000
mock_npc_dialog ... direct_challenge=0
```

这使父配置仍被跳过，故客户端继续收到 action 1 并打开零项菜单。修正后将同场景、
同 `monster_id`、`enabled=1` 的存在性检查并入动态 NPC 加载 SQL 的相关
`EXISTS(...)` 列；回调只消费这个已经返回的布尔列，绝不执行嵌套 MySQL 查询。

### 回归补充：未安装场景节点不得下发 action 13

在修正父记录加载后，运行时记录为：

```text
mock_npc_dialog actor=20021 ... direct_challenge=1
mock_direct_scene_challenge_reject ... requested_enemy=1000 req_index=0
reason=live-node-unready
```

同次登录的内容更新记录显示 `00蓬莱仙岛_02.sce` 仍为 403 字节的旧发布资源。
客户端因没有可见 kind-3 节点而在 `SendNPCInteractReq(0x01037ED4)` 中发出
`index=0`；这不满足 `mmBattle:0x66CC` 的 live-node 契约。`2/10+25/11` 不能
完成 action-13 的等待状态。

因此对话构造器现在先用同一场景、同一会话的
`vm_net_mock_select_sce_combat_spawn()` 验证资源和 27/11 节点顺序。验证失败时
省略 action 13、不给客户端制造 `4/1(index=0)` 请求，并在正常 `26/1` 对话中
说明“挑战目标尚未部署”。完成显式部署、完整重启并重新进入场景后，才重新显示
直接挑战项。

## 验证边界

- 改动后，动态小猴子不再因全局目录遗漏 `1000` 而降级成内置普通 NPC；对话走
  action 13，不会再进入零项副本菜单。
- 在旧 SCE 尚未重新部署前，客户端必须按真实场景节点状态拒绝直接挑战；不能用
  4/10 或伪造节点索引掩盖缺失资源。
- 重新部署后，资源必须通过当前 kind-3 field-18 校验，客户端安装新版 SCE 后才
  可发送并完成 `4/1 -> 4/5` 场景战斗链路。
