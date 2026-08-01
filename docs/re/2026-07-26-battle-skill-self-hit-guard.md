# 释放打怪技能却像把自己打死

日期：2026-07-26

## 现象

玩家报告：自己释放对怪技能后角色死亡。随附日志仅为任务/NPC（`6/11`、`6/4`、
黄蓉对话等），**没有** `mock_battle_operate` 行，无法从该片段直接定点。

补充（同日）：**服务端刚启动时正常，跑一段时间后出现**——与「进程内组队战
`party_count` 泄漏到后续单人战」相符，而非单次技能公式偶发。

## 代码侧可检验偏离

既有契约（`docs/re/2026-06-25-battle-server-flow.md`）已证明：

- subtype 5：`actor=1 → target=0` 才是打怪；
- `actor=0, target=1` 会让技能特效与负向 `valueA` 打到玩家身上。

相关风险点：

1. `enemy_hp_for_wire` 在 wire 无法映射到敌方时，曾回退为**全体敌方 HP 总和**，
   再配合错误目标写入 `actioninfo`，客户端会把放大后的技能伤害算到自己。
2. `apply_player_attack_targets` 在无存活候选时，曾无条件把 `requestedTargetSlot`
   塞进伤害列表，缺少「禁止玩家 wire」闸门。
3. `remember_last_operate` / auto 选目标把 wire `0` 当 falsy，subtype 5 下 `0`
   恰是合法怪槽，可能污染下一发自动目标。
4. **组队 wire 上下文泄漏（uptime）**：`g_vm_net_mock_team_battle_party_count_current`
   等为进程全局，不进 `account_capture`。请求结束时常只把 `active_account=NULL`
   而不清 `party_count`。此前若有人组队遇怪 / scene-poll `auto_pull`，下一名
   （或同一账号）单人 `4/6` 仍按 `party>=2` 映射线槽 → 技能打到玩家 wire。

服务端权威扣玩家血仍只走怪反击 `apply_damage_to_role`；因此「技能打死自己」
在表现上也可能是：同一条 `4/6` 里先播技能再播多怪反击，玩家把整回合死亡归因
到技能。需用 `mock_battle_operate` 的 `actor/target/rolehp/counters/counterdmg`
 区分。

## 修改

1. 未映射敌方 wire：`enemy_hp_for_wire` 返回 0；`damage_enemy_wire` 直接忽略。
2. 进攻结算只接受存活敌方 wire；跳过玩家 wire 并打 `self-hit-guard` 日志。
3. 写出 `actioninfo` 前若进攻目标等于 `playerSlot` 则 abort。
4. 记忆/自动战斗保留 wire `0` 为目标，不再用 `!= 0` 判断。
5. `vm_mock_service_clear_request_local_scratch`：在 `account_restore` 与每条
   协议请求结束清空组队 wire 全局；组队 `prepare` / `auto_pull` 会在真正组队
   路径上重新灌入。日志 `team_battle_context_clear reason=...`。
   后续补强见 `2026-08-01-mock-service-global-isolation.md`。

## 验证

1. `make -j2`，重启服务。
2. 复测放技能：日志 `actor` 应为玩家 wire、`target` 应为怪 wire；不应出现
   `self-hit-guard` / `reason=self-target`。
3. 长跑：先组队打怪，再换（或同）账号单人放技能；应先见
   `team_battle_context_clear`（若上一请求留下了 party），
   再 `mock_battle_operate` 为单人线槽。
4. 若仍死亡，请贴完整 `mock_battle_operate ... rolehp=0 counters=... counterdmg=...`
   一行：`counterdmg` 接近满血则是反击致死，而非技能自伤。
