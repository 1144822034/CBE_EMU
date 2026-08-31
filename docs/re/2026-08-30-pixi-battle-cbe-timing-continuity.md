# Pixi 战斗播报对齐 CBE 连续性与时序

日期：2026-08-30

## 症状

1. 鬼道（及施法席）起手像后退。
2. 技能特效 → 伤害飘字 → 怪物死亡退场分段、停顿明显，不像 CBE 一气呵成。

## CBE / mmBattle 契约（证据）

- 施法原地：`docs/re/2026-08-06-pixijs-battle-scene.md` — type1/2 播 anim6/7，**不**冲刺。
- 同包脚本：`battle_probe` 常见 `type0/1` 后紧跟 `type3`（`docs/re/2026-06-25-battle-server-flow.md`）。
- 特效：`effectIndex` → `eidolon.dsh` → `f_*.actor`（AnimEffect）；死亡 `f_death.actor`。
- 帧拍：mmBattle CBM 立即数集中在 100/160；Pixi 用 **100ms/part** 作为战斗条与 eidolon 步长。

精确 `DrawBattleCharacter` 计时仍 unresolved（需 IDA）；本改按资源帧数 × 100ms + 同拍重叠逼近观感。

## 根因（Pixi）

1. 无目标普攻按 `facingRight ? +18 : -18` 迈步；朝向异常时等于后退。施法虽不 tween，残留近战位移未钉回 `home`。
2. 播放串行过长：整段 cast → sleep → 整段 eidolon → 整段 hit(700ms) → settle(180) → 再 type3 死亡。CBE 是风起后 AnimEffect/伤害/死亡叠在同一拍。

## 修改（`BattleScreen.ts`）

1. `UNIT_FRAME_MS=100`；`CAST_WINDUP_FRAMES=2`；`HIT_FLASH_MS=200`；`FX_FRAME_MS=100`。
2. 施法：钉死 `home` 坐标；`windupOnly` 只等 2 帧后放行，eidolon/伤害并行叠在剩余 cast 上。
3. 无目标迈步改为**朝场地中线**（迎敌），禁止后退。
4. 致死记录后若紧跟 type3/4：在 impact 拍启动 `playDeathMotion`，消费后续 type3 避免重播。
5. 受击改为短闪；记录间 `waitBattleVisualSettle` 仅在下一手是另一 actor 时插入短拍。
6. eidolon 时长默认 `frames * FX_FRAME_MS`。
7. 脚锚 `battleFootAnchorX`：鬼道 cast 画布拉宽不再外移。

## 验证

1. 鬼道放仙术：站桩、不后退/外移。
2. 致死仙术：特效、伤害、死亡水花同拍；淡出约 780ms。
3. 多怪回合：仍串行 `playbackChain`。
4. 组队后手打尸：`isUnitDefeated` / `allOffensiveDefeated` 仍跳过冲刺。

## 续：击杀节拍

特效与伤害同拍；伤害后约 200ms 进灰化冻结，再约 280ms 接水花。尸体淡出改异步（不挡 `4/7` 结算面板）。致死记录后不再 `await` 残留 eidolon，避免「已死仍空等一会儿才结算」。

## unresolved

- IDA 未挂上 `DrawBattleCharacter` 精确帧定时与近战是插值还是瞬移。
