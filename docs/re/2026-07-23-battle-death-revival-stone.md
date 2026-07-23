# 战斗死亡后购买复活石的状态链路

Date: 2026-07-23

Status: revival persistence/client HP-refresh and ordinary-death respawn policy implemented; direct transport regressions passed

## 1. 当前卡点

- 触发：角色在战斗中死亡（HP 归零）→ 在死亡提示中进入商城 → 购买 `801 复活石` → 返回场景。
- 实际：角色持久 HP 仍为 `0`；场景侧不会再发起怪物战斗。
- 预期：复活石的确认使用必须结束死亡态、消费对应背包行，恢复满 HP、保留当前 MP，并通过正常场景进入状态统一推进；商城只负责购买，不能把 0 HP 留在一个无后续的死亡态。
- 最小目标：补齐客户端已经发出的死亡复活确认 `WT 1/7/14(result=1)` 的服务端处理，不把“开始下一场战斗时补 1 HP”当作复活流程。

## 2. 运行时证据

- 旧服务日志 `tmp/mock-service-19090.penglai-mijing-progress.stdout.log:1412-1419` 记录了取消/普通复活分支：

  ```text
  mock_battle_death_prompt_choice result=2 ... revive=49/41 ...
  source=builtin-battle-death-prompt-choice resp=75
  ```

  该历史分支曾落到出生场景，并已把 HP/MP 写为最大值的 30%。它证明死亡后的正常场景重入需要一个业务响应，不能只保存战斗内存状态；普通复活目的地已在 3.3 节按资源拓扑改为最近传送石场景。

- 当前 `vm_net_mock_build_shop_buy14_response`（`src/server/mock_server_interaction_login.c:1796-1915`）对 `801` 与其他商城物品相同：仅增加背包、扣 W 币、回 `1/14/3(seq,result)`。其中没有读取死亡态、没有改变 HP/MP，也没有结束战斗死亡状态。
- 修复前 `vm_net_mock_build_battle_death_prompt_followup_response` 只处理 `1/7/14(result=2)`；`result=1` 返回 0，随后会落入后续通用分派，未形成复活响应。
- 直接解析真实 `bin/JHOnlineData/item.dsh` 的第 168 行：`801 复活石` 的说明是“**使用后原地满血复活，免除死亡经验惩罚。**”；其类别为 `14`、`是否消耗=1`、`生命变化=0`、`法力变化=0`。因此 801 不是通用 HP 药，也不是商城购买即自动消耗的物品：它只在 `result=1` 的确认使用阶段恢复 HP，保留当前 MP，并且不改场景坐标、不扣经验/金钱。
- 死亡结算在 `src/server/mock_server_battle.c:2180-2192` 调用 `vm_net_mock_battle_save_completed_current_role_state("battle-operate-death")`，所以角色 HP=0 是已持久化的真实死亡状态，而不是 UI 漏刷新。

## 3. IDA 证据

| binary | function/address | findings |
| --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `HandleServerBattleCmd(0x7BD0)` | 接收 `1/4/8` 时读取 `autorevive`；死亡/复活状态由战斗模块的正常消息路径管理，不能写客户端对象绕过。 |
| `mmBattleMstarWqvga.cbm` | `RequestAutoRevive(0x3008)` | 先在客户端物品管理器中查询类别 `14`、ID `801`。有石头时走确认回调；没有石头时进入商城。 |
| `mmBattleMstarWqvga.cbm` | `Callback_WpayCancel(0x2FD0)` + `sub_2F10(0x2F10)` | 确认使用已有复活石时构造 `WT 1/7/14`，字段 `result=1`。该请求是复活石的权威服务端确认点。 |
| `江湖OL.CBE` | `net_handle_simple_result_info(0x01011434)` | `20/1` 简单结果用于清理主场景网络等待状态。 |
| `江湖OL.CBE` | `parse_scene_response_entry(0x010396D6)` | `30/1(scene,posinfo)` 通过正常场景进入流更新场景和坐标。 |

### 3.1 商城入口与死亡选择的分叉（2026-07-23）

用户新复现：角色死亡后看到“是否复活/进入商城”的提示，点击“是”时商城尚未完成打开便回到场景。

同次本地服务运行日志已取得，`tmp/mock-service-revival-stone.stdout.log:250-268` 先记录角色 HP 变为 `0`，随后记录：

```text
mock_battle_death_prompt_choice result=1 action=revival-stone
stone_seq=25 stone_remaining=0 revive=162/41
net_send ... wt=7/14 ... source=builtin-battle-death-prompt-choice
```

这证明客户端实际发送的是 `WT 1/7/14(result=1)`，且权威背包中确实存在一颗 801（序号 25），被本次消费后余量为 0；它不是无石头的“进入商城”分支。当时服务端随后发送的原地复活场景重入包解释了“迅速返回场景”，但该包绕过 Battle.cbm 属性结算，正是后续定位到的首次协议偏离；它不是商城切屏被服务端打断。

以下为 IDA 已确认、可检验的客户端契约：

| binary | function/address | 已确认行为 |
| --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `RequestAutoRevive(0x3008)` | 客户端仅以本地背包类别 `14`、物品 `801` 决定分支：有石头显示使用石头确认；无石头显示进入商城确认。 |
| `mmBattleMstarWqvga.cbm` | `Callback_WpayConfirm(0x2F4C)` | **无石头分支**收到确认键码 `0x1000`、`0x4000` 或 `0x20` 时，调用游戏管理器虚表 `+424` 打开商城、关闭提示，并只置商城返回标记 `+985`；这个成功分支不调用 `sub_2F10`，因此不发送 `WT 1/7/14`。非确认键码才调用 `sub_2F10(2)`。 |
| `mmBattleMstarWqvga.cbm` | `Callback_WpayCancel(0x2FD0)` | **有石头分支**确认时调用 `sub_2F10(1)`，即发送 `WT 1/7/14(result=1)`；非确认键码发送 `result=2`。 |
| `mmBattleMstarWqvga.cbm` | `sub_2F10(0x2F10)` | 仅当死亡提示请求标记 `+986` 已置位时构造 `WT 1/7/14(result=<1 或 2>)`，并立即清除此标记。 |

因此，服务端只有在实际收到 `1/7/14` 后才可以结束死亡分支。`result=1` 是复活石原地复活，必须走 Battle.cbm 的战斗结算终结链；`result=2` 是放弃后的普通复活，才走主场景的 `20/1 + 30/1`。无石头点击“是”若没有收到该请求，服务端不能主动发场景重入包；否则会打断商城切屏。

当前源码中 `1/7/14(result=2)` 确实会被解释为普通复活并返回 `20/1 + 30/1`。本次实测并未落入该分支，而是 `result=1` 的复活石分支。不能为了让该界面“继续打开商城”改写 `result=1` 或 `result=2` 的语义：前者会破坏有石头时的原地复活，后者会破坏明确放弃后的普通复活。

所需取证（不改变业务行为）：复现一次后保留同一时段的服务端日志。现有精确日志足以区分：

- `mock_battle_death_prompt_choice result=2 action=ordinary-respawn`：客户端实际到达了非确认/放弃分支；下一步检查确认控件传入的键码与回调来源。
- 没有该行：商城分支未发死亡确认，需以紧邻的 `net_send` / `unhandled` 行定位错误响应来源；不能把场景返回归因给死亡 handler。
- `result=1 action=revival-stone`：客户端当时已识别到本地 `801`，原地复活并返回场景是该分支预期，而不是商城入口。本次复现属于这一项。

### 3.2 首次状态偏离：只重进场景，没有走战斗结算属性同步（2026-07-23）

最新客户端复现表明：虽然服务端日志记录复活石成功、持久角色 HP 已写为最大值，返回场景后的客户端 HP 仍为 `0`。这否定了“仅有持久化正确即可完成复活”的先前验收结论。

首个偏离已定位在服务端成功响应，而非 801 的消费/HP 计算：当前 `result=1` 回的是
`1/20/1(result=0) + 1/30/1(scene,posinfo)`。它只清除主网络等待并切场景，完全绕过了仍在活动的 `mmBattle` 角色属性缓存。

IDA 交叉证据：

| binary | function/address | 契约 |
| --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `HandleBattleSettleMsg(0x743C)` | 战斗结算 `1/4/7` 读取 `hp`、`mp`，把它们写入本次结算的 HP/MP 变化量缓存。该对象还要求完整的 `exp/lastexp/curexp/persentexp/energy/energymax/gold/level/result/bagstatus/itemnum/iteminfo/autorevive` 字段顺序。 |
| `mmBattleMstarWqvga.cbm` | `HandleServerBattleCmd(0x7DF6..0x7E98)` | 后续 `1/4/8(autorevive=1,info)` 清除战斗标记并调用 `BattleSettle_UpdateCharAttrs()`，随后按客户端原生生命周期退出战斗。 |
| `mmBattleMstarWqvga.cbm` | `BattleSettle_UpdateCharAttrs(0x2C50)` | 把 `4/7.hp/mp` 的变化量加到当前战斗角色 HP/MP，按最大值截断，并同步主角色属性缓存；没有这个步骤，场景重入不会替换旧的 0 HP。 |
| 当前服务端既有胜利路径 | `mock_server_battle.c` 的已验证终结序列 | 已使用 `4/7 -> 4/8 -> 4/11 -> 4/9`，因此可复用该真实客户端终结契约，而不能另造只含 `30/1` 的复活快捷路径。 |

修改结果：复活石确认成功后在同一权威角色/背包状态中消费 801、写满 HP；响应改为完整的战斗终结状态同步。因为死亡时战斗客户端 HP 已是 0，`4/7.hp` 为已恢复后的完整 HP 增量，`4/7.mp=0` 保留现有 MP，随后发送已验证的 `4/8/4/11/4/9` 终结对象。不会同时发送 `20/1 + 30/1`，避免重入两套生命周期。

### 3.3 普通复活：最近传送石与两级经验惩罚（2026-07-23）

本次需求只作用于无复活石/放弃后的 `WT 1/7/14(result=2)`；`result=1` 的原地复活石链路不扣经验、不变更场景。

此前普通复活直接调用 `vm_net_mock_role_initial_scene_name()`，因此无论死亡点在哪里都会回到新手出生场景。这是服务端状态归属层的首次偏离：客户端已确认的 `20/1 + 30/1` 只规定“场景重入”，不规定目的地必须是出生场景，目的地与落点应由服务器的场景资源决定。

资源交叉验证结果如下：

| 数据 | 已确认语义 | 本次用途 |
| --- | --- | --- |
| `sMap.dsh` | 第 0/1/7-10/11 列分别为场景行 ID、场景名、上右下左相邻场景、所属世界地图 | 在同一局部场景图做 BFS，选择边数最短的传送石场景。 |
| `wMap.dsh` | 第 0/8-11 列为世界地图 ID 与上右下左相邻世界地图 | 局部图中没有传送石时跨世界图 BFS。 |
| 实际 `.sce` | 字面 actor 名 `n_telestone` | 仅将确有传送石 actor 的场景作为候选。 |
| `.sce` 出生/安全点解析 | `vm_net_mock_get_scene_reasonable_spawn_from_sce()` 后再经安全坐标校正 | 使用真实场景空间坐标落地。`sMap.dsh` 的 X/Y 是世界地图 UI 标记，明确不能作为角色坐标。 |

例如 `c00蓬莱仙岛_01.sce`（sMap 行 37）经本地相邻关系到 `c00蓬莱仙岛_03.sce`（行 39）的距离为 2；后者含 `n_telestone`，SCE 安全出生解析得到 `(157,47)`。局部图无候选时才以 `wMap.dsh` 的世界图距离选择候选，保证不会把未连通的任意传送石当作“最近”。资源缺失不会静默回退到出生点：有效源场景会保留当前场景并写 `mock_death_respawn_nearest_telestone_unresolved`，只有角色场景本身无效时才显式使用初始场景。

“掉两级”按等级阈值结算，而非仅减少当前等级的少量进度：普通复活令经验设为 `max(当前等级-2, 1)` 的起始经验。当前阈值为每级累计 100、300、600、1000、1500…；因此 5 级、1250 经验变为 3 级、300 经验，1/2 级均下限为 1 级、0 经验。原有 5% 金钱和 30% HP/MP 普通复活规则保持不变。

## 4. 调用链 / 首个偏差

1. 战斗 `4/6` 动作将 `g_mockBattleRoleHpCurrent` 降至 0，服务端保存 `role->hp=0`。
2. 客户端的 `RequestAutoRevive(0x3008)` 检查 `801`：没有时打开商城；购买成功的 `14/3` 只负责把物品写入客户端背包。
3. 客户端拥有 `801` 后，确认使用会发送 `WT 1/7/14(result=1)`。
4. 第一阶段修复前服务端只识别同类型的 `result=2`，因此 `result=1` 没有消费 801、没有恢复角色生命，也没有任何死亡收尾状态；随后错误地以 `20/1 + 30/1` 收尾，仍绕过了 Battle.cbm 的属性同步。

**根因陈述：** 第一阶段根因是死亡态与商城购买态之间缺少客户端已有的“确认使用 801”协议处理；补齐消费和持久化后，首个剩余偏离是仍以 `20/1 + 30/1` 场景重入代替 Battle.cbm 结算。该场景包不会更新仍持有 0 HP 的战斗角色缓存。正确修复是在服务端的 801 确认契约层发送完整 `4/7 → 4/8 → 4/11 → 4/9`，让客户端自身的 `BattleSettle_UpdateCharAttrs()` 更新主角色属性。`vm_net_mock_role_revive_floor_after_death()` 的下一场战斗补 1 HP 只是掩盖，已移除。

## 5. 请求 / 响应契约

### Request

- WT：`1/7/14`
- 关键字段：`result=1`（IDA `0x2F10`），且请求只含此对象。
- 前置条件：权威角色仍在死亡态（`role->hp == 0`）且背包存在 ID `801` 的有效行。

### Response

- `result=1`：服务端先原子消费一个 `801`，再将 HP 设为 `hpMax`（MP 保持死亡前的持久值）并持久化角色/背包；随后返回 `1/4/7`（完整结算字段，`hp=恢复后的完整 HP 增量`、`mp=0`）→ `1/4/8(result=1,autorevive=1,info)` → `1/4/11` → `1/4/9`。
- 上述 `4/7 → 4/8` 由 Battle.cbm 的既有结算路径将 HP 增量写回主角色缓存，随后终止当前战斗。`result=1` 不发送 `20/1` 或 `30/1`，不直接改客户端内存、场景或 battle 全局。
- `result=2` 是普通复活：服务端以 `sMap.dsh → wMap.dsh → SCE:n_telestone` 解析最近传送石场景及其安全落点，恢复 30% HP/MP，将等级降两级（最低 1 级且经验取该级起始值），并保留 5% 金钱惩罚；客户端响应仍为已验证的 `1/20/1(result=0) + 1/30/1(scene,posinfo)` 场景重生契约。商城购买本身仍不自动消耗 801。

## 6. Negative Evidence

- `vm_net_mock_role_revive_floor_after_death()`（`src/server/mock_server_equipment_npc.c:1119-1139`）只在下一场战斗起点将 0 HP 改为 1；历史日志已有 `mock_battle_start_zero_hp_floor`。它没有对应客户端确认包，也不能清理当前死亡 UI，因此不是可接受修复。
- 商城 `14/3` 只有 `seq,result`，战斗模块 `LoadBattleResourceData` 对它只清等待标志；它不是 HP/MP 同步消息，不能把购买响应伪造成复活结果。

## 7. 实现

1. `src/server/mock_server_interaction_login.c` 现在接受严格单对象 `1/7/14(result=1)`；仅在 `role->hp == 0` 且背包存在有效 `801` 时成功。成功结果固定为 `4/7 + 4/8 + 4/11 + 4/9`；不满足前置条件仅返回 `20/1(result=1,info)`，不伪造复活成功。
2. `src/server/mock_server_equipment_npc.c` 的 `vm_net_mock_role_apply_revival_stone()` 在同一权威角色/背包状态中消费一颗 `801`、设 `hp=hpMax`、保留 MP，并通过 `vm_net_mock_role_db_save("battle-revival-stone")` 持久化。
3. 移除了 `vm_net_mock_role_revive_floor_after_death()` 及其两处战斗启动调用。挂机和普通怪物触碰在 HP=0 时不再静默写入 1 HP，而是返回已有 parser-safe 的 `2/10 + 25/11` 死亡提示；完整复活仍必须走 `1/7/14(result=1)`。
4. 商城 `14/3` 仍只购买物品；成功购买 `801` 且角色死亡时只记录 `revival_confirm_pending=1` 供日志取证，不改变 HP。
5. `vm_net_mock_role_apply_death_penalty()` 现在只处理仍为死亡态（`hp == 0`）的普通复活；解析最近 `n_telestone` 场景后，在同一角色状态中写入落点、两级经验惩罚、既有金钱/HP/MP 惩罚并持久化。重复的 `result=2` 返回 parser-safe `20/1(result=1,info)`，不再次扣除或重入场景。

## 8. 验证

- [x] `result=1` 被严格 detector 命中，服务日志为 `source=builtin-battle-death-prompt-choice`；返回完整的 `4/7 + 4/8 + 4/11 + 4/9`，不含 `20/1` 或 `30/1`。
- [x] 2026-07-23 在本机 MySQL 夹具运行 `php tmp/battle-revival-stone-regression.php run 19090`：死亡角色使用 801 后，数据库验证 HP `148/148`、MP `31`、经验 `321`、金钱 `654`、场景 `c00蓬莱仙岛_01.sce`、位置 `(220,454)` 保持；背包 801 数量从 `2` 到 `1`。测试同时断言 `4/7.hp=148`、`4/7.mp=0`、`4/8.autorevive=1`、`4/11/4/9` 存在，且不含 `20/1` 或 `30/1`；非死亡时重复 `result=1` 被拒绝且不会再消耗石头或改写状态。
- [x] HP=0 时不再由战斗启动路径静默补 1 HP。
- [x] 2026-07-23 在本机 MySQL 夹具运行 `php tmp/battle-ordinary-respawn-regression.php run 19090`：从 `c00蓬莱仙岛_01.sce` 的死亡角色以 `result=2` 复活，响应只含 `20/1 + 30/1`，数据库验证 5 级/1250 经验变为 3 级/300 经验、金钱 `654→621`、HP/MP 为 `45/38`、场景为 `c00蓬莱仙岛_03.sce` 且安全坐标为 `(157,47)`；紧随的重复 `result=2` 被拒绝且数据库行不变。
- [x] `make -j2` 已通过。
- [ ] 客户端人工回归：死亡→商城购买→返回→确认使用；以及缺石/重复确认、普通 `result=2` 复活和重新登录后触怪。购买日志的 `revival_confirm_pending` 与随后 `mock_battle_death_prompt_choice result=1` 可用于对照实际路径。
