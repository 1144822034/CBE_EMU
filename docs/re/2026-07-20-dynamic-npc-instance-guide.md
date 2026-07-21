# 动态副本向导 NPC

## 目标

让后台动态 NPC 能声明副本传送和守关怪挑战能力，而不是把副本目标硬编码到普通传送石或场景怪物处理器中。实现必须继续由客户端请求和服务端响应推进，不修改 CBE 全局状态。

## 客户端证据

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `task_hall_activate_selected_entry(0x010492B0)` | 对话项 `action=1` 发送 `26/1 {type=2,id=value}`，适合作为服务端动态菜单入口。`action=26` 会发送 `30/<value>`，但目标配置不随请求携带。 |
| `江湖OL.CBE` | `net_handle_scene_channel_dispatch(0x01039B8A)` | `30/1` 由场景进入解析器消费；`30/9` 进入挑战确认文本分支。 |
| `江湖OL.CBE` | `HandleChallengeResponse(0x010395AA)` | `30/9` 读取 `isleader` 和 `challenge`，显示确认界面；它不是战斗开始包。 |
| `江湖OL.CBE` | `HandleResConfirmCb(0x01039566)` | 确认界面结束后发送空的 `30/10`，不能单靠该包恢复动态 NPC 的目标场景和怪物 ID。 |
| `江湖OL.CBE` | battle response dispatcher | 非场景节点战斗使用既有 `1/4/10 {side,battleinfo}` 契约；场景怪物触碰继续使用 `1/4/5`。 |

因此动态副本配置由服务端按 NPC Actor ID 查找。客户端仍通过已经验证的 `action=1 -> 26/1` 选择“进入副本”或“挑战守关怪”。传送返回 `30/1 scene+posinfo`；NPC 挑战必须先返回 `30/9 {isleader,challenge}`，由客户端关闭菜单请求的等待状态并显示确认框。确认回调发出空 `30/10` 后，服务端才返回非场景怪物战斗包 `4/10`，避免把向导节点误当作怪物节点。

## 数据模型

`server_dynamic_npcs.npc_kind=6` 表示副本向导。副本专有字段放在一对一扩展表 `server_dynamic_npc_instances`：

- `target_scene`、`target_x`、`target_y`：副本场景和进入落点；
- `challenge_enemy_id`：守关怪目录 ID，`0` 表示不提供挑战；
- `minimum_level`：传送和挑战共用的最低等级；
- 主键、外键均为 `(scene, actor_id)`，删除动态 NPC 时自动级联。

没有把这些列继续塞进 `server_dynamic_npcs`，从而保持普通、任务、商店、修理和技能 NPC 的基础行稳定。独立迁移脚本为 `server/mysql/migrate_add_dynamic_npc_instances.sql`；服务启动也会执行同样的 `CREATE TABLE IF NOT EXISTS`。

## 后台行为

动态 NPC 的服务类型新增“副本向导（传送／挑战）”。选中后才显示：

- 副本目标场景；
- 落点 X/Y；
- 挑战怪物 ID；
- 最低等级。

目标场景可留空以创建仅挑战 NPC，怪物 ID 可填 `0` 以创建仅传送 NPC，但两项不能同时关闭。落点 X/Y 同时为 `0` 时，保存流程从目标 SCE 解析安全入口；手动坐标必须位于目标场景像素范围内。挑战怪物必须存在于当前服务端怪物目录，避免客户端收到不存在的战斗模板。

## 协议实现

服务端私有 action=1 值：

```text
EAxxxxxx  打开副本菜单，低 24 位为 NPC Actor ID
EBxxxxxx  进入副本
ECxxxxxx  挑战守关怪
```

传送会记忆正常的 scene-change target、保存角色位置并返回 `30/1`。挑战分为两个请求阶段：

1. `26/1 {type=2,id=ECxxxxxx}` 校验副本向导和等级，返回 `30/9`，并在当前服务连接上保存 Actor ID、怪物 ID、场景、坐标和确认时间；
2. 客户端确认回调发送严格的 9 字节空 `30/10`。服务端只在同一连接存在未过期且场景一致的待确认记录时消费它，构造内部 `4/1 {id,index,posx,posy}` 请求，再调用共享战斗 builder 的 `forceNonSceneStart=true` 入口返回 `4/10`。

待确认记录按 `clientId` 隔离，断线时清理，60 秒后或场景改变时作废。普通场景怪物的原有 `4/1 -> 4/5` 路径不变。

## 验证

- `make -j2`：通过。
- 后台页面回归：成功登录 `127.0.0.1:19091/admin-418yz6/`，页面包含副本类型及全部条件字段，HTML 167174 字节。
- 后台保存回归：临时 Actor `39990` 的 X/Y 均填 `0`，自动解析为 `(223,370)`，扩展表保存怪物 `105`、最低等级 `1`。
- 协议回归（初版）：副本菜单响应 141 字节；进入响应 53 字节并包含 `1/30/1`。
- 2026-07-20 客户端实测修正：挑战菜单选择先收到 `1/30/9`，客户端随后发送空 `1/30/10`，第二个响应才包含 `1/4/10`。运行日志依次为 `mock_npc_instance_challenge_prompt`、`mock_npc_instance_challenge_confirm` 和 `mock_challenge_battle_start subtype=10 scene_start=0`。
- 临时 NPC 及扩展表记录已级联删除，测试角色坐标已恢复。
