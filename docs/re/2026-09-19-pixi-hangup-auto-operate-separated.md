# Pixi 挂机与自动战斗技能记忆分离

日期：2026-09-19

## 症状

挂机选群体技能长时间后变成普攻；蓝够、非超时。停挂再开有时恢复，有时需重登。

## 根因

挂机与 `[自动]` 共用客户端 `lastBattleOperate` / 服务端 `LastOperate`：

1. 挂机开战把所选技能写入 `lastBattleOperate`。
2. 挂机 alone 的 `4/2`（含误发普攻）经 `remember_last_operate` 改写 `LastOperate`。
3. 黑屏 soft-reload 只恢复 `hangupDesired`，不恢复 `hangupSkillOperate`，且 `selectRole` 清角色态时序会把技能清成 0。

## 契约（分离后）

| 开关 | 技能记忆 | 出手 |
|------|----------|------|
| 挂机 alone | 客户端 `hangupSkillOperate`（仅挂机选技能对话框写入） | 客户端 `4/2` + `hangup=1` |
| `[自动]` | 客户端 `lastBattleOperate` + 服务端 `LastOperate` | gateway poll synth |
| 挂机 `4/2` | **不**写 `LastOperate` / `lastBattleOperate` | — |
| 开 `[自动]` | **不**用 `hangupSkillOperate` 种 `LastOperate` | — |

## 修改

- `GameSession`：挂机 / 自动各自读记忆；`noteManualBattleOperate("hangup")` 空操作
- `buildBattleOperateRequest`：可选 `hangup=1`
- `mock_server_battle.c`：`hangup=1` 时 `remember_last_operate` 只更新目标 wire
- GL soft-reload 快照恢复 `hangupSkillOperate`，并 `startHangup()` 重武装

## 验证

1. 挂机选群攻连打：服务端不应因挂机 `4/2` 出现 `auto11_seed_operate operate=0`；`LastOperate` 保持开战前（或仅手动/[自动]写入）
2. 挂机选群攻 → 停挂 → 手动放另一技能 → 开 `[自动]`：应放手动技能，不是挂机群攻
3. 挂机中 soft-reload：恢复后仍放原挂机技能
4. 故意选挂机「普通攻击」：稳定普攻，且不锁死之后的 `[自动]` 技能记忆
