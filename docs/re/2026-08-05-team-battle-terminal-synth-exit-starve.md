# 2026-08-05：组队终局 timeout-synth 后阿狸退场、队友卡死

## 症状

三人组队（相互 / 蔡文姬 / 阿狸）挑战，战斗动作尚未播放：

- 阿狸直接退出战斗画面回到地图
- 另外两人停在 Battle.cbm 无法操作

## 触发与日志锚点

`team_battle_queue` → 两名队友先 `4/2` 打死怪 → `terminal-wipe` 给阿狸补座时
`playback-hold` skip → 随后 `round-timeout` synth 在阿狸账号上下文做
`team_battle_round_terminal_release`（`resp=659` 进丢弃的 `synthResp`）→
`team_battle_action_queue ... delivered=00 terminal=1` →
`team_battle_state ... finished=1` → **没有**任何
`team_battle_action_deliver` → 阿狸 poll 到 `mock_battle_settlement_exit`
（`4/8+4/11+4/9`）退场 → 队友只收到 `team-hsp`。

## 根因（第一次偏离）

1. **`team_battle_release_if_settled` 残桩**（`mock_server_recovered_helpers.inc`）
   在 `battleFinished=1` 后立刻把 `battleActive=0`，**不检查**未投递的
   `battleEvents`。`build_pending_team_battle_action_response` 要求
   `battleActive`，共享 `4/6` 永远无法 poll 投递。与
   `docs/re/2026-08-04-team-battle-finished-latch-spar.md` 契约相反。

2. **scene poll 顺序**：`settlement_exit` 优先于 team action。synth 终局在阿狸
   账号上 `note_victory_settlement` 武装 `4/8`，即使 battleActive 仍在，下一次
   poll 也会先拆 Battle.cbm，跳过共享动作包。

3. **次要**：`terminal-wipe` 路径仍尊重跨场残留的 solo `playback_hold`，导致
   空闲席位不能立刻补齐终局屏障，放大了 timeout-synth 走阿狸账号的概率。

## 修改

1. 删除 recovered 残桩；在 `mock_server_battle.c` 实现
   `release_if_settled`：仅当无未投递事件、无 pending `4/5` 时
   `team_clear_battle_state`。
2. scene poll：team action / team start **先于** settlement exit。
3. AutoSynth `terminal_release`：不武装 solo `settlement_exit`（清掉持有）；
   各席位在 poll 投递终局 `4/6` 时再 `note_victory_settlement`。
4. （历史）`terminal-wipe` 曾忽略跨场 `playback-hold`；随后即时 wipe 本身因抢先播放被撤销，见
   `docs/re/2026-08-05-team-battle-terminal-wipe-premature-playback.md`。

## 复测

1. 三人组队，一人后手；另外两人先出手打死怪。
2. 日志应有三人 `team_battle_action_deliver ... terminal=1`，然后各自
   `settlement_exit`；不得出现 `finished=1` 后立刻无 deliver、阿狸单方面 `4/8`。
3. 邻接：非终局超时补刀、正常最后一席真人 `4/2` 释放、胜后再切磋
   （`finished-settled` clear）仍正常。

## 仍未知 / 风险

- MySQL `socket send failed` 与本卡住正交，但可能干扰战后落库。
- 若客户端在收到终局 `4/6` 前就本地拆战斗 UI，仍需对照 IDA 时序。
