# 2026-08-07：切磋回合只播首击 / 收不到对方打击

phase: spar-merged-4-6-peer-hit-drop
status: fixed

## 症状

双方已进切磋后：

- 被切磋方提交动作后看不到本方出手动画
- 切磋方也收不到 / 播不出被切磋方的打击

## 契约（预期）

服务端切磋与组队同为回合屏障：

1. 先提交的 `4/2` → `duel_round_defer`（零对象 WT）
2. 双方都提交后 `duel_round_resolve`：按 `firstTurnIndex` 每击一条 `4/6`
   （`current_team_count=1` / `InitActionSlot_B`），同 WT merge 投递
3. 末席在 `4/2` 直回拿包；先出手席靠 scene-sync poll 的
   `pending_duel_action`

客户端须按对象顺序播完**全部** `4/6`，并在 defer 期间锁菜单。

## 第一次偏离

### A. Pixi 只解析第一个 `4/6`

`parseBattleAction` 使用 `objects.find(kind=4, subtype=6)`。
合并包里第二击（常为后手 / 被切磋方）被静默丢弃。

与 CBE 遍历全部 WT 对象的行为偏离；崩溃/无动画是最终症状。

### B. 切磋未置 `awaitingRound`

`noteOperateSubmitted` 仅在 `right.length >= 2`（组队）时锁菜单。
切磋 `right.length === 1`，defer 空包后菜单可重开，易误当成「没出手」。

### C. poll 门闩（对齐组队修复）

`pending_duel_action` / `terminal` 排在 `sceneVisibleReady` 之后；
组队 `team_battle_action` 已提前。Battle.cbm 占用 UI 时先出手席可能饿死 peer 包。

## 根因

首个错误状态：合并 WT 的第二（及后续）`4/6` 未进入 `BattleActionUpdate.actions`，
播放脚本从未看到被切磋方打击。

## 修改

| 层 | 改动 |
|----|------|
| `protocolExtras.parseBattleAction` | `filter` 全部 `4/6`，按序合并 actions / hpDeltas / teaminfo |
| `BattleScreen.noteOperateSubmitted` | 一律置 `awaitingRound`（单人同包 `applyUpdate` 清锁） |
| `mock_server_social` poll | `duel_action` / `duel_terminal` 提到 `sceneVisibleReady` 之前（紧随 team action） |

## 验证

- `npx tsx scripts/smoke-protocol.ts`：双 `4/6` merge 断言两击 deltas
- `make -j2`
- 双端切磋：双方各普攻一回合 → **两段**攻击动画；双方 HP 均掉

## Negative

- 不要把两击硬塞进单条 `actioninfo`（会破坏 `InitActionSlot_B` / MP）
- 不要在缺 `4/6` 时客户端自算伤害补动画

## 附：切磋屏蔽逃跑（同日）

- Pixi：spar（`4/10` left `visual0`=sexGroup 1..2）菜单去掉「逃跑」，软键右键空；强按提示「切磋中无法逃跑」
- 服务端：`duel_escape` 回 `4/4 result=0`，不结束 duel
