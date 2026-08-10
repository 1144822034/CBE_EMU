# Pixi 组队战：全员女鬼道 + 群体技能误伤队友

## 症状

1. 组队进战右侧人物造型都像本地角色（如女鬼道）。
2. 队长放群体技能后，队友播受伤/死亡动画。

## 根因

### 造型

`4/5` `battleinfo` 右侧按**冻结队员顺序**（队长在 `[0]`），不是「自己永远在 `[0]`」。

Pixi 曾把 `right[0]` 强制刷成本地 `role` 造型；队友若未按 wire id 命中 roster，又回落到 `playerActor`，于是全员同一套本地外观。

### 误伤

组队 `subtype=5` 的 actioninfo wire 使用 team 置换（`docs/re/2026-06-25-battle-server-flow.md`）：

```text
M=1 P=2: leader→wire2, member→wire0, monster→wire1
```

Pixi 仍用单人表（player=1, monster=0），把怪目标 wire1 映射到 `right[0]`，群体技能打怪的伤害落到队长/队员槽并播死亡。

## 修复

1. `battleUnitForWire` / `battleTargetWire`：`party>=2` 走 team `wire_to_display`。
2. `onBattle`：按 `slot.id` / `selectedRoleId` 填充每名成员 job/sex/actor；血蓝取自己那一席。
3. `BattleScreen.rebuildUnits`：队友优先用自身 job/sex，禁止一律 `playerActor`。

## 验证

两人组队遇怪：造型分职性别正确；队长群体技能只伤怪，队友不播死亡。
