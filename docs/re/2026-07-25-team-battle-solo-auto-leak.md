# 组队战斗被单人挂机自动出手穿透

日期：2026-07-25

## 现象

双人组队遇怪后：

1. 一方已正确 `team_battle_round_defer`（`acted` 未满 `alive`，`resp=5`）。
2. 另一方（常为刚挂机/开过自动的队长）仍出现 `prefer=1` + `mock_battle_auto_synth` / `auto12` → 单人 `mock_battle_operate`（`bundle=1`）。
3. 回合不等齐就结算；线槽/死亡位按单人语义映射，技能可打到己方（日志 `deaths=1 deathActor=0` 等）。

## 根因

单人挂机/中途自动（`g_mockBattleAutoPrefer` + scene-poll / `4/12` synth）调用的是**单人** operate builder，不走 `vm_net_mock_build_synchronized_team_battle_response`，因此：

- 跳过 `team_battle_round_defer` 回合栅栏；
- 使用单人 wire / `bundle` 语义，与组队 party 线槽不一致。

触发条件：进入组队战前 prefer/挂机环仍为 1，或开战包仍附带 `4/11` 并 `auto_arm_pending`。

## 修改

`src/server/mock_server_battle.c`：

1. `vm_net_mock_battle_suspend_solo_auto_for_team`：清 prefer、挂机环、auto pending。
2. 组队开战：队长 `challenge-team-start` / `team-battle-leader-start`、队友 `team-battle-deliver` 时 suspend；开战包不在 `party>=2` 时附带 `4/11` / arm pending。
3. `vm_net_mock_battle_auto_seat_can_act`：组队战中恒 false，阻断 synth。
4. `4/11` / `4/12`：组队战中强制 suspend，只 ACK（不 synth、不 keep-prefer）。
5. hangup-loop poll：组队战中不投递。

## 验证

1. 双账号组队；一账号先前开过挂机/自动。
2. 遇怪：应见 `mock_battle_suspend_solo_auto` / `mock_team_battle_start`，**不应**再出现 `mock_battle_auto_synth` / `prefer=1` 的 `auto12` operate-only。
3. 一方出手后另一方 defer（`resp=5`），齐手后 `round_release` / 合并 `4/6`。
4. 技能目标为敌方线槽，不应把队友写成 `deathActor`。

## 风险 / unresolved

组队内中途自动已改为走 synchronized + round_defer，见
`docs/re/2026-07-25-team-battle-auto.md`。开战仍清挂机 prefer，需在战中再点自动。
