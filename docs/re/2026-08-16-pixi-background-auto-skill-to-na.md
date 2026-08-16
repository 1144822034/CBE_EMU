# Pixi 息屏/切后台后自动战斗变成普攻

日期：2026-08-16

## 症状

手机端 APK 开挂机或战内 `[自动]` 后切后台 / 息屏，过一会出手从已选技能变成普攻；回前台后往往仍普攻。

## 运行时契约

| 开关 | 出手权威 |
|------|----------|
| `[自动]` | gateway `prefer` + poll `auto_choose_operate`（`LastOperate`） |
| 挂机 alone | 客户端 `4/2`（`hangupSkillOperate`，可回落 `lastBattleOperate`） |
| 跨场重开自动 | `setBattleAutoPrefer(true)` → `1/4/11` 可选种 `Operate` |

## 根因

1. **跨场重开 `[自动]` 无条件种 `Operate=0`**  
   `lastBattleOperate == null` 且 `hangupSkillOperate == 0` 时仍带 `Operate` 字段。  
   服务端 `auto11_seed_operate` → `remember_last_operate(0)`（非 synth）把已有技能记忆锁成普攻。  
   息屏挂机连场时，每场开战都会 `setBattleAutoPrefer(true)`，放大该路径。

2. **挂机 alone 忽略上一手技能记忆**  
   `inBattleAutoOperate` 在挂机单开时只读 `hangupSkillOperate`；未配置（默认 0）时固定普攻，并经 `onAttack` → `noteManualBattleOperate(0)` 把客户端记忆也冲掉，后续再开 `[自动]` 继续种 0。

3. **自动普攻与手动普攻共用 `onAttack`**  
   挂机/`submitAutoOperate(0)` 走 `onAttack`，与手动攻击一样把 `lastBattleOperate` 写成 0。

蓝不足时的 poll `mp_fallback` 普攻（synth）本来就不覆盖 `LastOperate`，不是本题；回蓝后应恢复技能。

## 修改

| 层 | 行为 |
|----|------|
| `GameSession.setBattleAutoPrefer` | 仅在有明确记忆时带 `Operate`（含故意普攻的 `lastBattleOperate===0`）；未设置则省略字段，保留服端 `LastOperate` |
| `GameSession.inBattleAutoOperate` | 挂机技能为 0 时回落 `lastBattleOperate` |
| `GameSession.noteManualBattleOperate` | `source:"auto"` 且 `operate==0` 时不擦除已有非 0 记忆 |
| `BattleScreen.submitAutoOperate(0)` | 改走 `onSkill(0)`，由 main 按自动源记笔记 |
| `protocolExtras` 注释 | 标明勿默认种 `Operate=0` |

## 验证

1. 手动放技能 → 开 `[自动]` → Home 息屏 2–5 分钟（FGS 保活）→ 回前台：仍应放同一技能；日志 `auto_synth operate=skillId+2`，跨场重开不应出现 `auto11_seed_operate operate=0`（无记忆时无 Operate 字段）。
2. 挂机选技能 → 息屏连打：客户端 `4/2` / poll 均为该技能。
3. 挂机/自动对话框故意选「普通攻击」：应稳定普攻；手动普攻后再开自动：应普攻。
4. 蓝耗尽：仍可 `mp_fallback` 普攻，回蓝后恢复记忆技能。
