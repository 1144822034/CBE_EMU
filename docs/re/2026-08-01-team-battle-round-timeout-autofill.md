# 组队战斗：队友超时未操作自动普攻

日期：2026-08-01

## 现象

组队进战后若某队友长时间不提交 `4/2`（卡 UI、掉线边缘、商城残留等），其余已操作队友卡在回合屏障（`team_battle_round_defer` / `acted` 不齐），整队无法推进。

## 契约

对齐切磋 `duel_round_timeout_autofill`：

1. 回合打开后武装截止（`CBE_TEAM_BATTLE_ROUND_TIMEOUT_MS`，默认 20000；`0` 关闭；非 0 下限 3000）。
2. 超时仍存活且未 `acted` 的座位由服务端合成 `Operate=0` 普攻，走既有 `synchronized_team_battle` 屏障。
3. 不修改客户端内存；不跳过真实 4/6 合并投递。就绪队友通过 poll 收到合并行动后继续。

## 修改

1. `vm_mock_service_team.battleRoundActDeadlineTick`：回合截止（scheduler tick）。
2. `team_battle_ensure_round_deadline`：存活座仍欠操作时懒武装。
3. `team_battle_try_round_timeout_autofill`：超时后按座位切换 account/session，合成普攻；同轮已释放则停。
4. 触发点：`synchronized_team_battle` 入口、`pending_team_battle_action` poll。
5. 回合 release / `team_battle_clear` 后 deadline 清零，下一回合重新武装。

## 验证

1. `make -j2`
2. 三人组队进战：两人数秒内操作，一人停手 >20s → 日志
   `team_battle_round_timeout_autofill ... operate=0`，其余人收到合并 `4/6` 并进入下一回合或结算。
3. 正常齐射（无人超时）不应出现 timeout_autofill。
4. `CBE_TEAM_BATTLE_ROUND_TIMEOUT_MS=0` 关闭该门闩。
