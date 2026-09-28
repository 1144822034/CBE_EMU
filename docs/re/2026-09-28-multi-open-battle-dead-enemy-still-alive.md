# 多开切号：组队战斗已死敌方仍显示存活

日期: 2026-09-28

## 触发

1. 多开两号同队进战斗（或一号观战/出手、另一号后台）。
2. 在 A 号界面打掉敌方（或队友在共享 4/6 里击杀）。
3. 切到 B 号战斗界面。

## 预期 / 实际

- 预期：B 号看到已死敌方为尸体隐藏（或至少 hp=0 不画活体）。
- 实际：敌方模型仍 idle「活着」。

## 根因

1. **flush 竞态**：`switchAccountSlot` → `flushPendingBattle` 先 `onBattle(pending)`（内部 `await getBattleSkills`/`openBag` 后才 `battle.show`），立刻再 `applyUpdate`。此时 `BattleScreen.state == null`（切号 `resetForAccountSwitch`/`hide` 已清），`applyUpdate` 直接 `return`，死亡回合丢弃。
2. **视觉态未入快照**：死亡靠 `UnitView.dead`/`hidden` + 水花，不在 `BattleState`。`show`→`spawnUnit` 一律 `dead:false`，即便 `left[].hp===0` 也画活体。
3. **后台未折 HP**：非活跃槽只排队 `pendingBattleUpdates`，`pendingBattle` 常停在入场满血；队列截断（64）时更易丢击杀包。

## 修改

| 位置 | 改动 |
|------|------|
| `BattleScreen.applyUpdate` | `!state` 时入 `pendingUpdates`，`show()` 末尾 drain |
| `BattleScreen.spawnUnit` | `side.hp<=0` → `dead`/`hidden`，跳过活体展示 |
| `main.ts` gateUi `onBattleUpdate` | 后台用 `foldBattleActionHpIntoState` 折入 `pendingBattle` |
| `protocolExtras.ts` | 新增 `foldBattleActionHpIntoState` |

## 验证

1. 两号多开组队打怪；A 号打死一只后切 B → 该怪应已隐藏/不可选。
2. B 号一直后台、A 打完整场再切 B → 敌方与结算态一致，不残留活体。
3. 单号切号进出战斗无回归（菜单/自动/出手正常）。

## 风险

- 切回后可能回放积压 4/6 动画（原设计）；已 `hp<=0` 隐藏的单位 `playDeathMotion` 会 early-return，避免二次水花。
