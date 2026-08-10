# Pixi 挂机战内「关闭」条

日期：2026-08-10

## 需求

挂机进战后，战斗场景右上角提供与 `[自动]` 同款的状态条，点「关闭」停止挂机。

## 契约

- 展示：`hangupActive` 时 `挂机战斗中` + `关闭`（与 `autoBattleActive` 的 `自动战斗中` 共用同一 host；挂机优先）。
- 点击：`session.stopHangup()`（先清 `hangupDesired`，再发 `2/10 Type=2`），与世界 HUD 停挂相同。
- 权威：本地不提前清 `hangupActive`；听 `1/3/3`「下一场完成后挂机停止」/「已停止挂机」。

## 修改点

| 文件 | 内容 |
|------|------|
| `BattleScreen.ts` | `setHangupActive` 刷新条；`closeHangup` / `onHangupCancel` |
| `main.ts` | `battle.onHangupCancel` → `stopHangup()` |
| `GameSession.ts` | `stopHangup()`；chat 标志先于 UI |
| `2026-08-08-pixijs-battle-auto-vs-hangup.md` | 验证补挂机条 |
| `2026-08-10-pixi-hangup-keepalive-resume.md` | 战内停不掉根因与修复 |

## 续：未挂机点 HUD 先选技能再开战

- 未挂机：`openHangupSkillDialog(true)` → action 260 记技能后 `startHangup()`。
- 已挂机：仍直接 `startHangup()` 停环（与战内关闭条相同）。
- 菜单「挂机战斗」仍只记录（`startAfterPick=false`）。

## 验证

1. 挂机进战：右上角「挂机战斗中」+「关闭」。
2. 点关闭：聊天/tip「下一场完成后挂机停止」或「已停止挂机」；本场后不再自动遇怪。
3. 仅 `[自动]`：仍为「自动战斗中」+「关闭」，行为不变。
4. 地图未挂机点挂机 → 技能菜单 → 选后开战；取消不开战。
5. 挂机中再点挂机 → 不停在菜单，走停环提示。
