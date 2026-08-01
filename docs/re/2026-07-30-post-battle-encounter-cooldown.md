# 战斗离场后遇怪冷却

Date: 2026-07-30

Status: **cancelled** — 遇怪冷却门闩已关闭

```text
phase: settlement_exit -> (no challenge gate)
reason: 冷却拒绝曾推 25/11，卡住左上角「斗」，无法再遇怪；
        改为 25/12+聊天后仍无法可靠恢复，按产品要求取消限制。
```

## 当前行为

1. `CBE_BATTLE_ENCOUNTER_COOLDOWN_MS` 默认 **0**；`arm_encounter_cooldown`
   **不再**设置 `NotBefore`，挑战 `4/1` **永不**因冷却拒绝。
2. 离场后仍可能投递一次 `25/12`（poll / 开战前置），仅用于清掉旧构建留下的
   「斗」/banner，不拦截开战。
3. 挂机续场逻辑不变。

## 验证

1. 打完怪回地图立刻碰怪：应直接
   `mock_challenge_battle_start ... enemies=`，**无**
   `reject-encounter-cooldown`。
2. 左上角无「斗」；能连续遇怪。
3. `make -j2 server`。

## 相关

- `2026-07-25-spar-visual-and-terminal-crash.md` §22
- `2026-07-30-pve-exit-empty-settle-box.md`
