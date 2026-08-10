# PixiJS 战内[自动]与挂机开关分离

日期：2026-08-08

## 需求

1. 战斗菜单 `[自动]` 单独开关，与世界 HUD / 菜单「挂机」分离。
2. `[自动]` **不**自动遇怪：仅在玩家已进战后，按挂机技能设置或上一手玩家操作自动提交 `4/2`。
3. 菜单「挂机战斗」选技能时**只记录** `hangupSkillOperate`，不调用 `startHangup`。
4. 未挂机时点 HUD 挂机按钮：先弹技能菜单，确认后再 `2/10 Type=2` 开战；已挂机再点则停环。

## 契约

| 开关 | 状态 | 遇怪 | 战内出手 |
|------|------|------|----------|
| `[自动]` | `autoBattleActive` | 否（玩家碰撞/挑战等） | `lastBattleOperate ?? hangupSkillOperate` |
| 挂机 | `hangupActive`（`2/10`） | 是（服务端挂机环） | `hangupSkillOperate` |

二者均可同时为真；出手路径共用 `shouldInBattleAuto()` / `fireInBattleAuto`。

## 修改点

- `GameSession`：`autoBattleActive`、`lastBattleOperate`、`shouldInBattleAuto`、`inBattleAutoOperate`、`noteManualBattleOperate`
- `BattleScreen.runMenu("自动")` → `onAutoToggle`
- `main.ts`：进战 / 回合更新走 `fireInBattleAuto`；action `260` 只记技能；攻击/仙术写入 `lastBattleOperate`
- 战内右上角条：`自动战斗中` + `关闭`（`setAutoBattleActive` / `closeAutoBattle`）；手动攻击/仙术/物品/逃跑或点关闭 → `onAutoCancel` 清开关并恢复卷轴菜单
- 挂机进战时同位置条：`挂机战斗中` + `关闭`（`setHangupActive` / `closeHangup` → `onHangupCancel` → `stopHangup()` 清意图并再发 `2/10 Type=2`）；本场可打完，听服务端「下一场完成后挂机停止」/「已停止挂机」
- 再开自动不动作：出手不再依赖 `session.inBattle`（战中场景包可能清掉）；`armInBattleAuto` 在座位可操作前重试；`onAutoReady` 在回合播完后补射

## 未接

- 真包 `1/4/11` 旗标与菜单隐藏仍 unresolved；当前为客户端等效行为。

## 验证

1. 菜单挂机战斗选技能 → 聊天「已记录技能…」且**不**出现「已开始挂机」
2. 未挂机点 HUD 挂机 → 技能菜单 → 选技能后开战 + 用所选技能出手；战内右上角见「挂机战斗中」+「关闭」
3. 挂机中再点 HUD 挂机 / 战内关闭 → 「下一场完成后挂机停止」或「已停止挂机」；本场后不再续挂
4. 手动遇怪 → 菜单开 `[自动]` → 用上一手或挂机技能连打；关自动后恢复手动菜单
5. 仅开自动、不开挂机 → 打完一局后**不会**自动再遇怪；条为「自动战斗中」
