# 组队战斗中途自动（走回合栅栏）

日期：2026-07-25

## 背景

先前为堵住挂机 `prefer` 漏进组队、用单人 `operate` 绕过 `round_defer` 打到队友，
曾在组队战中强制 `4/11 type=0` 并禁止 synth。结果是三人（及双人）组队里无法用自动。

## 正确契约

组队自动与单人共用客户端 `4/11`/`4/12` + prefer 环，但服务端合成出手必须：

1. 经 `vm_net_mock_build_synchronized_team_battle_response`（含 `round_defer` / peer deliver）
2. 仅当该座位本回合尚未出手且仍存活时可 synth
3. 开战仍 `suspend` 挂机 prefer（防挂机环带进组队）；战中可再点自动启用

## 修改

- `seat_can_act`：组队按 `actedMask` / HP / leftMask 判断，不再恒 false
- `auto_synth`：一律走 synchronized operate；`g_mockBattleAutoSynthInProgress` 避免误标 `client_driven`
- 取消 `auto11`/`auto12` 在组队中的强制 suspend
- 开战清 prefer / 不附带开战 `4/11` 的防护保留
- `pull_team_vitals`：选目标/`arm_pending` 前把共享队伍 HP/party_count 拉进 globals（否则线槽按单人算、HP 门控失败）
- `prefer-poll-rearm`：账号 restore 会清 `pendingArmed`；组队多客户端下若 `prefer=1` 且可出手则在 scene poll 重新 arm，否则永远不行动
- 组队不因真实 `4/2` 置 `client_driven`（客户端常只发一次就等回合栅栏）
- 组队 `4/12` 不再反复 `arm_flag`（避免 flag 饿死 operate）；team auto tick 默认 8

## 验证

1. 三人组队遇怪；任一点自动：应见 `mock_battle_auto_synth ... team=1`，非终席为 `round_defer`/`resp=5`
2. 随后 poll 有 `prefer-poll-rearm` / `mock_battle_auto_poll_deliver team=1`，每回合继续出手
3. 三人齐 prefer 或混手动：齐手后合并 `4/6`，无队友 `deathActor`
4. 单人挂机/自动回归仍正常
5. 显式 `4/11 type=0` 后组队不再 poll 出手
