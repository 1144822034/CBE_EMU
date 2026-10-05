# 刚进梦境副本死亡后「普通复活 / 商城购买」没反应

日期: 2026-10-04

## 触发

1. 刚进入梦境 `29*`（使者进本 30/1，进图后仍在拉 HUD / 属性 / 拍卖，或同壳 catalog/poll）。
2. 立刻开战死亡，弹出死亡面板。
3. 点「普通复活」或无石时的「商城购买」无离场、无开商城。

## 预期 / 实际

- 预期：点「普通复活」发 `1/7/14 result=2`，`20/1+30/1` 离开战斗落到传送石；无 801 点「商城购买」打开商城，返回后仍在死亡面板。
- 实际：点击无后续。软键被 `awaitingRound` 吃掉；进图迟到的 `30/1` / `1/1/21` 把死亡座当活着同图进图或直接拆战斗；`pickNpcOption.then` / `onEnterScene` 把已打开的商城抢回战斗或世界。

## 契约 / 首次偏离

刚进梦境时 `onEnterScene` 仍在跑（`fetchDreamHudRemainingMs`、属性、拍卖），且 `hp<=0` 时任何 `30/1` 都被当成普通复活离场。这与「必须先确认 `1/7/14`」冲突：客户端未点复活，进图后续包已经改了 `inBattle` / `session.hp` / `phase`。

| 预期 | 实际（修前） |
|------|----------------|
| 死亡面板可点，直到 `1/7/14` | `handleSoftkey` 在 `awaitingRound` 时直接 return |
| 无石「商城购买」立刻 `openSystem(mall)` | `deathPromptBusy` 先锁死；进本 `.then` / 迟到 `showPhase("world")` 抢走商城 |
| 同壳 `30/1` 且未确认复活 → 留在死亡面板 | `hp<=0` 即 `applyBattleLeaveSceneEnter`，或 `1/1/21` 把 hp 写回非 0 后 `keepBattle` 丢弃真正的 `20/1+30/1` |

## 修改

| 位置 | 改动 |
|------|------|
| `GameSession.scenePacketLeavesDeadBattle` | 仅 `20/1` 或已武装普通复活才离场 |
| 同壳 enter/landing/reload | 死亡且未确认则 keep death prompt |
| `prepareLiveExchange` / `1/1/21` | 战斗中不 poll、不覆盖座位 HP |
| `BattleScreen` | 清 `awaitingRound`；死亡层抬到怪血条之上；商城不清忙锁 |
| `main` | 死亡软键可用；迟到进图不拆死亡/商城 overlay |

## 验证

1. 进梦境后立刻战死 → 一次「普通复活」应回图（本图传送石或出本落点），可走动。
2. 无 801 点「商城购买」应打开商城；关商城回到死亡面板，有石后可原地复活。
3. 野外同图普通复活、传送后立刻遇怪留战，仍按 2026-10-03 文档。
