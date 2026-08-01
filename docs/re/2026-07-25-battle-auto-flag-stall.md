# 战斗内自动战斗开关

Date: 2026-07-25

Status: implemented (server-driven continuous ticks)

## 压缩结论

| 现象 | 根因 | 修复 / 结论 |
| --- | --- | --- |
| `4/11` 无响应卡住 | 未处理 | `flag-ack` / `operate-only` |
| 同包夹带行动 | 前缀 → `4/12`；后缀群体 → 卡死 | **禁止**同包夹带 |
| `rearm=1` 常亮不放技能 | `type=1` 只再生 `4/12`，从不发 `4/2` | **禁止**对 `4/12` 回 `type=1` |
| 点一次只打一次 | 中途按钮路径进不了挂机式连续 `4/2`；`4/12` 曾清掉 `prefer` | 保持 `prefer`；首击 `operate-only`；poll 补旗标 + 行动 |
| 第一场点自动无自动态，第二场才有 | 开战包可挂非按钮 `4/11`；中途按钮 ACK 会 `4/12` 拨回 | 中途启用后 **poll 补发开战同款 `4/11 type=1`**（`flag_poll_deliver`） |
| 客户端连续 `4/2` | mid-button ≠ hangup 开火态 | poll 非按钮 `4/11` 对齐开战路径；真实 `4/2` 后 `client_driven` 停 poll 合成 |

## 契约（当前）

1. `4/11 type=1`（按 1 开）→ 与挂机开战同机：`prefer=1` + `HangupStyleFlagOk` +
   arm poll；**只 ACK `4/11`**，不内联 operate
2. `4/11 type=0`（按 1 关）→ 清 `prefer` + pending；**战斗结束不清**
3. `4/12` 且 `prefer=1` → **空 ACK**；不内联 synth、不补 `flag_poll`
4. scene-sync poll：`auto_choose_operate` → `via=auto-poll`（与挂机同一选技/蓝不够普攻）
5. 开战若 `prefer`：challenge/hangup 开战包带 `4/11 type=1` + `arm_pending`
6. **唯一差异**：进战方式——手动 `4/1` vs 挂机定时拉怪；战内出手逻辑相同
7. 回合间隔：`CBE_BATTLE_AUTO_TURN_GAP_MS`（默认 **0**）。`playback_hold` =
   动作播放时长 + 可选间隔

## 负向证据

- `rearm=1` 循环：无 `4/2`，倒计时结束无技能
- `operate-only` 后若清 `prefer`：只能点一次打一次
- 开战 `arm_pending` 先开 3s 窗 + 真实群攻 `4/2` 回 `4/11`：人物不动作（见 `2026-07-27-hangup-auto-group-skill-stall.md`）

## IDA

- Request：`Callback_Unknown2@0x2BF1` subtype 11 / subtype 2 `@0x2CB5`
- Response case 11 `@0x7cb7` → `sub_263b(8)`
- unresolved：如何让中途按钮进入与挂机开战相同的客户端连续 `4/2` 开火态

## 验证

1. 手动技能 → 按 1：`auto11 ... hangup-style-arm` + `4/11`；首击来自
   `mock_battle_auto_poll_deliver` / `auto_choose`（同挂机）
2. 蓝不够：`mp_fallback` 普攻，回蓝后恢复记忆技能
3. 群体 / 多怪连续，无 `rearm=1`、无 4/11 内联 operate
4. 按 1 关闭后不再出手；胜利后下一场仍自动（`auto_keep`）
