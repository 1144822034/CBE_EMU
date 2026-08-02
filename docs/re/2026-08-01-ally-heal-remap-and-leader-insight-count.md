# 单体治疗被改到自己 / 队长战斗心得固定 3 怪

## 症状（运行时日志）

1. 组队战鬼道点选队友施放 `清风拂体`（operate=213 → skill 211，`目标指向=1`），治疗落在自己：
   ```
   team_battle_state ... slots=0/20/0
   mock_battle_single_ally_heal operate=213 request_index=1 actor=1
     target_member=1 wire=3 amount=0
   ```
2. 组队遇怪数量未固定为 3；同段日志 `insight_bonus_percent=0`、
   `mock_exp_card_status ... insight=0`（本局队长只有经验卡 ×30，无 828）。

## 根因 1：友方治疗仍走 `select_live_enemy_wire`

契约注释要求友方治疗保留原始 `4/2.index`，但 operate builder 在进治疗分支前仍无条件调用 `select_live_enemy_wire`。

组队 wire 表下敌方 0 与客户端「队友座 0」数字重叠。当 `slots=0/20/0`（敌 0 已死）时：

1. 客户端点选队友 → raw index `0`（或队伍 action 表中的队友 wire）
2. `select_live_enemy_wire` 判定 wire0 对应敌 0 已死 → 改写为下一活敌 wire `1`
3. `resolve_friendly_heal_member(1)` → `target_member=actor`（自己）
4. 自己满血时 `amount=0`，表现为「选了队友却加自己 / 没加到血」

## 根因 2：队长战斗心得

掷点入口 `battle_roll_enemy_count` 已按 active role 的 828 固定为 3。组队开战由队长发起，但队员 skill/durability 加载可能短暂弄脏 active role。组队路径在写入 team battleinfo 前再按**队长 session role** 复核一次 828。

本次用户日志中 `insight=0`，故未出现 `enemy_count_fixed`、遇怪仍为 1..3 随机——与「未开心得」一致。开 828 后应见 `battle-insight` / `battle-insight-leader` 且 `enemies=3`。

## 修改

1. `mock_server_battle.c`：友方 heal/buff 跳过 `select_live_enemy_wire`（两处 operate builder）。
2. `mock_server_battle.c`：`vm_net_mock_battle_apply_leader_insight_enemy_count`，challenge/hangup 组队开战在 build team battleinfo 前套用。
3. 保留 `CBE_BATTLE_ENEMY_COUNT` 环境强制优先级。

## 验证

1. `make -j2 server`，重启。
2. 双人组队，先打死一只怪再让鬼道点选残血队友放清风拂体 → 队友回血；日志
   `single_ally_heal` 的 `target_member` 为队友座，且不再在敌 0 死后变成 actor。
3. 队长使用 828 后再遇怪 → `mock_battle_enemy_count_fixed ... reason=battle-insight` 或
   `battle-insight-leader`，`mock_challenge_battle_start ... enemies=3`。
4. 未开 828 / 仅经验卡时仍为 1..3 随机。
