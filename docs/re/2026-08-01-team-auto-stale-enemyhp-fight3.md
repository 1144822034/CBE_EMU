# 组队自动：第3场开战 enemyhp=0 导致不行动

日期：2026-08-01

## 症状

第2场 prefer 自动正常；第3场开战包 `prefer=1` 但 `pending=0`，双方不合成，卡到超时。

## 运行时证据

```text
# 第2场结束 enemyhp=0/60，prefer 保留
mock_challenge_battle_auto_keep prefer=1 last=1 operate=23 party=2
mock_battle_team_auto_keep ... pending=0
mock_challenge_battle_start ... enemies=3 enemyhp=0/60 ...  # 应为满血
mock_challenge_battle_auto prefer=1 ... pending=0
auto_timers ... prefer=1 pending=0 next_act_ms=<上一场 hold>
```

## 根因

1. `reset_enemy_hp` 后立刻 `team_start_prepare_auto` → `pull_team_vitals`。
2. 上一场 `battleFinished` 仍 `battleActive`，pull 把 **enemyhp=0** 盖回 globals。
3. `team_begin_battle` 用这套死血做新共享快照 → 新场一开就是全灭。
4. `arm_pending` / `seat_can_act` 因敌方已灭拒武装 → `pending=0`。

## 修改

1. `pull_team_vitals`：`battleFinished` 时不拉取。
2. 队长 challenge：先 `begin_battle`，再 `team_start_prepare_auto`；begin 前再 seed 一次。
3. `begin_battle`：若拷贝后 `enemyhp==0` 则按 stats 重 seed。
4. `arm_pending_at_start`：清掉上一场 playback hold。

## 验证

1. 组队 prefer 连打 3 场以上：开战日志 `enemyhp` 应为满（如 60/60），`pending=1`。
2. 不应再出现 `team_battle_enemy_hp_reseed`（若出现说明仍有 stomping，需继续取证）。
