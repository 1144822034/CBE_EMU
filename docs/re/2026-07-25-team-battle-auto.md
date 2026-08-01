# 组队战斗中途自动（走回合栅栏）

日期：2026-07-25

## 背景

先前为堵住挂机 `prefer` 漏进组队、用单人 `operate` 绕过 `round_defer` 打到队友，
曾在组队战中强制 `4/11 type=0` 并禁止 synth。结果是三人（及双人）组队里无法用自动。

## 正确契约

组队自动与单人共用客户端 `4/11`/`4/12` + prefer 环，但服务端合成出手必须：

1. 经 `vm_net_mock_build_synchronized_team_battle_response`（含 `round_defer` / peer deliver）
2. 仅当该座位本回合尚未出手且仍存活时可 synth
3. 开战清**挂机环**（防地图挂机 4/1 带进组队），**保留 prefer**；开战包可带 `4/11 type=1`
4. prefer 跨场保留至按 1（`4/11 type=0`）；与单人一致

## 修改

- `seat_can_act`：组队按 `actedMask` / HP / leftMask 判断，不再恒 false
- `auto_synth`：一律走 synchronized operate；`g_mockBattleAutoSynthInProgress` 避免误标 `client_driven`
- 取消 `auto11`/`auto12` 在组队中的强制 suspend
- `team_start_prepare_auto` 替换原 `suspend_solo_auto`：只清 hangup，保留 prefer 并 arm
- 队长 challenge / 队友 deliver：prefer 时附带 `4/11 type=1`
- `pull_team_vitals`：选目标/`arm_pending` 前把共享队伍 HP/party_count 拉进 globals
- `prefer-poll-rearm`：组队 poll 在 `prefer=1` 且可出手时重新 arm
- 组队 `4/12` 不再反复 `arm_flag`（避免 flag 饿死 operate）

## 验证

1. 组队遇怪 → 按 1：`mock_battle_auto_synth ... team=1`，非终席 `round_defer`/`resp=5`
2. poll：`prefer-poll-rearm` / `mock_battle_auto_poll_deliver team=1`，每回合继续
3. 打完再遇怪：`team_auto_keep` / `auto_keep`，无需再按 1
4. 齐手后合并 `4/6`，无队友 `deathActor`
5. 按 1 关闭后不再 poll 出手；单人挂机/自动回归正常
