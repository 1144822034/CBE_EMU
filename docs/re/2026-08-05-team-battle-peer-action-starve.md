# 组队战斗：先出手席位卡在 Battle.cbm 直到掉线

日期：2026-08-05

## 症状

组队战斗中（高压/多端更易复现）：

1. 队长或先出手席位操作后没有技能动画；
2. 全员操作后，旁观席可见怪死，施法席卡在战斗 UI；
3. 卡住直到断线，而不是等到下一回合。

## 运行时证据

`bin/dlog.txt`（组队回合屏障）：

```text
team_battle_round_defer ... resp=5          ← 先出手：空 ack，无 4/6
team_battle_round_release / terminal_release ← 末席：合并 4/6（+结算）
team_battle_action_queue ... delivered=02    ← 仅收刀席已标记投递；先出手仍欠包
```

随后长时间无 `team_battle_action_deliver` / `team-battle-peer`，直到超时或断线。
终局时仍可见 `timeout_autofill` 等满约 20s 才填空闲席。

## 根因

契约是**回合屏障**，不是“谁收刀谁先结算”：

1. 非末席 `4/2` → `resp=5` defer，共享 `4/6` 入队且 `deliveredMask` 不含 defer 席；
2. peer 包依赖 scene-sync poll 的 `build_pending_team_battle_action_response`；
3. 该投递原排在 `sceneVisibleReady` 门闩之后，且晚于 solo-auto；高压下 poll 席被饿死。

崩溃/卡死 PC 是 Battle.cbm 等 `4/6` 的最终症状；第一次偏离是 **release 后 peer 包未及时投递**。

## 修改

1. **Poll 优先级**：`team_battle_action` / `team_battle_start` 提到 map vitals 之后、solo-auto 之前，且不再受 `sceneVisibleReady` 门闩阻塞。
2. **already-acted**：若本席仍欠共享 `4/6`，优先投递 pending action，而不是空 `already-acted` ack。
3. **solo-auto 让路**：本席欠 team action 时 flag/operate synth 返回 0。
4. 增加 `team_battle_action_deliver` 日志便于回归。

> **作废**：曾加入的「终局立刻 `via=terminal-wipe`」会在第三人操作前释放合并 `4/6`。已撤销，见
> `docs/re/2026-08-05-team-battle-terminal-wipe-premature-playback.md`。
> 空闲席仍由 `CBE_TEAM_BATTLE_ROUND_TIMEOUT_MS` 超时 autofill。

## 验证

1. 两人组队：队长先出手 defer，队员收刀 → 队长应迅速出现 `team_battle_action_deliver`，有技能动画并离开战斗 UI。
2. 三人终局：第二人收刀后不应出现 `via=terminal-wipe`；第三人操作（或超时）后才 `terminal_release`。
3. `make -j2` 通过。
4. 自伤/技能打己仍属 unresolved（本 capture 无 `self-hit-guard` 证据）。

## unresolved

- 高压下技能打自己：尚无本轮日志证明；需另取证 `party_count` / ally remap。
