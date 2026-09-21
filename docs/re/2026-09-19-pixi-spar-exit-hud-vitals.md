# 2026-09-19：切磋/比武结束后左上角血蓝条未还原

## 现象

切磋或比武结束后回到地图，玩家头像旁 `WorldHud` HP/MP 条仍是战内残血/残蓝。

## 根因

1. 切磋/比武座位 HP/MP 只活在 duel 状态；服务端 `onlineHp/Mp`（地图耐久）不随战损改写（强制 PK 败亡惩罚除外）。
2. `4/7.hp/mp` 对切磋固定为 `0`（对齐 PvE 默认显示增量；`sparExitRecoverHp` 算了但不下发）。
3. Pixi 战中 `onVitalsChanged` / `onBattleUpdate` 把座位写进 `session.hp/mp`；`endBattleToWorld` 再把座位刷到左上角 → 地图条跟了战损。

## 修改

- 进切磋/比武时快照 `session` 血蓝。
- `endBattleToWorld` 若 `battle.isSparBattle()`，用快照还原后再 `hud.setVitals`；PvE 仍走座位（含 `4/7` recover）。

## 验证

1. 切磋受伤后胜利/失败 → 回地图左上角与开战前一致。
2. 比武擂台同测。
3. 普通打怪残血胜利（无壶）→ 回地图仍为战末残血，不被快照影响。

### 续：失败方仍不刷新（2026-09-19）

`battle.hide()` 在撕座位前调用 `notifyVitalsChanged` → `onVitalsChanged` 用座位
HP 写回 `session`。失败方座位为 0，把刚还原的开战快照盖掉；胜方座位仍有血，
看起来像「只有失败方不刷新」。

`endBattleToWorld` 改为先算好目标血蓝，`hide()` 后再写回 `session` 并 `setVitals`。
胜负双方均回满血蓝（`hp=hpMax` / `mp=mpMax`），不沿用开战残血快照。
赏金擂台：`EVT_FIGHT` 置 `wagerMapHealLatch`，与切磋/比武共用离场回满路径。
