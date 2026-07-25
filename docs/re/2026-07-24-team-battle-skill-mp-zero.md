# 组队战斗技能法力：归零 / 第二次回满 / 末击回蓝

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 第一次放技能后 MP=0 | 合并 `4/6` 缺/少 `teaminfo`，`unit+1344` 仍为 0 | 按队员数写齐 overlapped `teaminfo`；peer 多行重写 |
| 第二次放技能 MP 回满 | `battleMemberMp` 被无依据写回更高值（日志 `357→367`），下一轮 `teaminfo` 发出满值 | 组队禁止 `role->mp` 覆盖当前 MP；`finish` 拒绝非吃药/回蓝的 MP 回升 |
| 末击技能后立刻回蓝，结算界面自动回蓝=0 | 击杀回合在 merge 前 inline 结算：`auto-flask` 抬高 `g_mockBattleRoleMpCurrent`，merge 的 `teaminfo` 已是满蓝；merge 又剥掉 kind=4，客户端先看到满蓝回写，后见的 `4/7.mp` 恢复量却是 0 | 组队禁止 operate/item inline 结算；merge（及既有 terminal_release）在写完扣费后 `teaminfo` 再追加 `4/7`；结算种子 MP 用战斗当前值而非 durable `role->mp` |

## 契约

`InitActionSlot_B` 按 `current_team_count` 读 `teaminfo` 行 → `unit+1344`；
type-1 结束 `currentMP = unit+1344`。行值必须是当前战斗 MP（施法者为扣费后）。

结算 `4/7` 的 `mp` 字段是**回蓝增量**（flask / `CBE_BATTLE_RECOVER_MP`），不是绝对蓝量。

## 验证

重启 `jh-online-server.exe`。两人组队：

1. 同一角色连放两次技能：应连续扣费，不回满。
2. 最后一击技能：动画过程中蓝量保持扣费后值，不应立刻回满。
3. 结算界面：无 flask/`CBE_BATTLE_RECOVER_MP` 时自动回蓝显示 0；有自动药回蓝时显示与实际恢复一致。

若仍回满，查日志 `team_battle_mp_refill_blocked` 与 `mock_battle_settle ... recover=`。
