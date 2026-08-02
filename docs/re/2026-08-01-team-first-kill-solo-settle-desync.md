# 组队首刀清场：队长被 solo 结算拆场、队友卡战斗

日期：2026-08-01

## 症状

双方进入组队战斗后操作卡住；过一会儿队长退出战斗场景，队友仍留在
Battle.cbm；队长再遇怪又卡住。

## 运行时证据（lxh001 队长 + lxh003 队员，enemies=3）

```text
team_battle_round_prepare ... source=55c1611d actor=0 resolve=0
mock_battle_settlement_exit_arm reason=battle-operate-victory  ← 错误
team_battle_round_terminal_capture ...
team_battle_round_capture ... actions=4
team_battle_state ... slots=0/0/0 finished=0
team_battle_round_defer ... resp=5

team_battle_round_prepare ... source=7b11e416 actor=1 resolve=1
team_battle_round_terminal_release_failed ...
mock_battle_operate_abort reason=no-targets slots=0/0/0
unhandled wt=4/2

mock_battle_settlement_exit ... response=4/8+4/11+4/9  ← 队长被拆场
```

## 根因

1. 队长 AOE 首刀打死全部 slot（洞察 enemies=3、每怪 20HP）→
   `battleEndsThisRound`。
2. solo operate builder **无视** `party_count>=2`，仍
   `note_victory_settlement` / `settlement_exit_arm`，并清
   `OperateSessionArmed`。
3. 同步包装层因 `resolve=0` 只发 defer ACK（resp=5），**不投递** 4/6+4/7。
4. 队员齐回合后走 `terminal_release`；若失败则 `4/2` unhandled，双方卡住。
5. 队长 poll 到延后 `4/8` 拆场；队员仍停在战斗 UI。

被违反的契约：组队胜利用 merge / terminal_release 统一下发 4/6+4/7；
非最终席位的杀招只能 capture+defer，不得武装 solo `settlement_exit`。

## 修改

1. `battle-operate` / fallback：`party_count>=2` 时禁止 solo
   `note_victory` / 清 armed；并清掉误武装的 settle pending。
2. `prepare_operation`：ensure_multi 只作用本座 globals，terminal 路径不写回队伍。
3. `terminal_release`：胜利门闩改 slot 判定；失败打明确 reason 日志。

## 验证

1. 组队 + 洞察三怪：队长首刀清场 → 日志**无**
   `settlement_exit_arm reason=battle-operate-victory`；有
   `terminal_capture` + defer。
2. 队员出手 → `team_battle_round_terminal_release` 成功，双方收到
   4/6+4/7，再延后 4/8。
3. 不应再出现队长已回地图、队员仍卡战斗。
