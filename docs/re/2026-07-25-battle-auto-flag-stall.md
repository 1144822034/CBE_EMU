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

1. `4/11 type=1` 且可行动 → `operate-only`，`prefer=1`，`suppressNext12=1`，`arm_flag_pending` + `arm_pending`
2. `4/11 type=1` 且暂不可行动 → `flag-ack` + 同上 pending
3. `4/11 type=0` → 清 `prefer` + 全部 pending（显式关闭）
4. `4/12` 且 `prefer=1` → `type=0` **保留 prefer**；补 `arm_flag_pending`；**永不** 对 `4/12` 回 `type=1`
5. scene-sync poll（先于 `sceneVisibleReady`）：
   - 先投 `4/11 type=1`（`mock_battle_auto_flag_poll_deliver`，对齐开战）
   - 若客户端仍不 `4/2`，再投 `operate-only`（`via=auto-poll`）
6. 真实客户端 `4/2` 且 `prefer` → `client_driven=1`，停止 poll 合成（避免双发）
7. 开战若 `prefer`：开战包已带 `4/11`，只 `arm_pending` 作兜底
8. 回合间隔：`CBE_BATTLE_AUTO_TURN_GAP_MS`（默认 **0**，无取消窗）。`playback_hold`
   = 动作播放时长 + 该可选间隔。非 0 时仅抑制 poll 合成下一击；真实 `4/2`
   **必须**仍回 `4/6`。见 `2026-07-28-auto-button-cancel-hangup.md`。
9. 旗标延迟：`CBE_BATTLE_AUTO_FLAG_DELAY_TICKS`（默认 8≈0.8s）仅用于中途补发开战同款 `4/11`

## 负向证据

- `rearm=1` 循环：无 `4/2`，倒计时结束无技能
- `operate-only` 后若清 `prefer`：只能点一次打一次
- 开战 `arm_pending` 先开 3s 窗 + 真实群攻 `4/2` 回 `4/11`：人物不动作（见 `2026-07-27-hangup-auto-group-skill-stall.md`）

## IDA

- Request：`Callback_Unknown2@0x2BF1` subtype 11 / subtype 2 `@0x2CB5`
- Response case 11 `@0x7cb7` → `sub_263b(8)`
- unresolved：如何让中途按钮进入与挂机开战相同的客户端连续 `4/2` 开火态

## 验证

1. 手动技能 → 点自动 → 立刻一击（`via=auto11`）
2. 约 3s 后无需再点 → 再击（`via=auto-poll` / `mock_battle_auto_poll_deliver`）
3. 群体 / 多怪同样连续，不卡死、无 `rearm=1`；日志不应再出现 `mock_battle_auto_turn_gap_wait`
4. 显式 `4/11 type=0` 或逃跑后不再 poll 出手
