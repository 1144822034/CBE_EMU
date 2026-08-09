# 组队战斗 HP/MP 权威状态与客户端缓存（2026-08-09）

Date: 2026-08-09

Status: implemented; build verified; runtime regression pending

## 1. 当前卡点

- 可见现象：组队战斗中施放一次法术后，客户端 MP 可能被清空；战斗离开后 HP/MP 与本次战斗
  实际损耗不一致。
- 触发方式：两名在线角色进入同一场怪物战斗，一名队员施放法术，另一名队员提交本回合操作并
  释放合并的 `4/6` 动作。
- 本轮最小目标：让组队回合的每份 `4/6` 保留客户端所需的全队 MP 快照，并禁止队伍屏障完成前
  走单人战斗结算/自动恢复/持久化路径。

## 2. 运行时证据

`bin/server_out.txt` 的最近一轮（行 947--1004）显示：

1. 战斗初始快照：角色 `10036` 为 `615/615 HP, 925/925 MP`。
2. 该角色提交 `Operate=233`：`mock_battle_operate` 正确计算 `mp=925/890`，即法术仅应扣除
   35 MP。
3. 同一请求因击杀最后怪物，在队伍的另一名存活成员尚未提交前就出现了
   `mock_battle_settle ... team_victory=0` 和 `mock_battle_auto_flask ... mp=35`。这说明内层通用
   builder 把“怪物 HP 已为 0”误当作单人终局，提前运行了 `4/7` 与自动壶恢复。
4. 随后的 `team_battle_state` 又记录该成员为 `rolemp=925/925`，覆盖了本回合正确的 `890`。
5. `team_battle_round_release` 构造合并 `4/6` 时调用的是不含 `teaminfo` 的
   `vm_net_mock_append_battle_action6_object()`；队友收到的共享 action object 同样没有这一字段。

## 3. IDA 证据

| binary | function/address | findings |
| --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `InitActionSlot_B(0x6DBC)` | 读取 `teaminfo`，对每个当前战斗单位读一行重叠 tagged-i32 数据；行中的角色/战斗 wire id 匹配该单位时，将第三个值写入 `unit+1344`（MP 缓存）。 |
| `mmBattleMstarWqvga.cbm` | `HandleBattleActionMsg(0x6EB0)` | 在解析 `actioninfo` 前调用 `InitActionSlot_B`。 |
| `mmBattleMstarWqvga.cbm` | `DrawBattleSceneBg(0x4BE8)` | type-1 技能播放结束使用单位的 MP 缓存恢复可见 MP；未写入的缓存为零。 |

`InitActionSlot_B` 的现有反编译确认：每个行的三个重叠读起点依次为 row+0、row+4、row+8；
正确编码为 `00 04, id32, hp32, mp32`，每行 14 字节。客户端循环的是当前战斗成员数量，且只
按 id 匹配写入相应单位。因此组队回合不能只携带施法者行，更不能完全省略 `teaminfo`。

## 4. 调用链 / 首个错误状态

1. 客户端发送 `WT 1/4/2 { index, Operate }`。
2. `vm_net_mock_build_synchronized_team_battle_response()` 调用通用
   `vm_net_mock_build_battle_operate_response()`，后者正确生成该成员的法术与 MP 扣除。
3. 当怪物 HP 变为 0 时，通用 builder 尚不知道队伍回合屏障，会追加单人 `4/7`、自动壶恢复，
   并调用单角色终局写库；这是权威状态的首次错误写入。
4. 组队 wrapper 随后把 actioninfo 缓存，直到本轮最后队员提交；
   `vm_net_mock_merge_team_battle_round_response()` 重新包装 `4/6`，但使用的 helper 不带
   `teaminfo`，是客户端 MP 缓存的首次缺失。
5. 客户端解析合并 `4/6`，技能动画结束后从零 MP 缓存恢复；随后错误显示可继续覆盖场景 HUD。

## 5. 实施的修复

- 活动组队操作中，通用战斗 builder 现在只产生 `4/6` actioninfo 与共享临时快照；它不再内联
  `4/7`、不再触发自动壶、不再调度单人终局关闭，也不再写单角色终局状态。物品操作同样遵守该
  边界。
- 若本回合含有需要 MP 缓存的 type-1 行为，真正释放的组队 `4/6` 构造完整 `teaminfo`：每位成员
  一行，MP 取当前动作后的共享快照；已缓存的先行动作保留其 `teaminfo` 需求，避免被最后一个普通
  攻击覆盖。
- 共享 action 投递给其他观察端时，逐行重写所有成员的 wire id；不再只改第一个施法者行。
- 普通回合的真正终局移动到 `vm_net_mock_merge_team_battle_round_response()`：仅在全队动作合并后
  才附加 `4/7`、自动壶、掉落刷新及源角色持久化。此前已有的“击杀者先行动、其余成员补交操作”
  路径也采用同一份全队 `teaminfo`。

## 6. 静态验证

- `git diff --check` 通过。
- `make -j2` 于 2026-08-09 通过，生成 `bin/jh-online-server.exe`。
- 未启动服务端、未修改用户进程、端口或 `jh_online` 数据库；还需用原始两客户端步骤进行运行时
  回归，确认日志中的 `team_battle_round_release ... teaminfo=28`（两人）以及两端 HUD 与场景 HSP
  一致。

## 7. 运行时验证清单

- [ ] 非终局的两人一法术回合，合并 `4/6` 有 2 行 `teaminfo`，施法者 MP 为扣费后值。
- [ ] 队员先击杀全部怪物、另一人后提交时，在真正 release 前没有 `mock_battle_settle`、
  `mock_battle_auto_flask` 或单人终局保存。
- [ ] 两端各自收到的 action object 中均有完整、观察端 wire id 对应的 `teaminfo`。
- [ ] 终局后服务端、场景 HSP 与客户端 HUD 的 HP/MP 一致。
- [ ] `make -j2` 通过；不修改客户端内存、寄存器或 CBE/CBM。
