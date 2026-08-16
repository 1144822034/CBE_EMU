# PixiJS 战内[自动]与挂机开关分离

日期：2026-08-08（2026-08-16 对齐 gateway prefer）

## 需求

1. 战斗菜单 `[自动]` 单独开关，与世界 HUD / 菜单「挂机」分离。
2. `[自动]` **不**自动遇怪：仅在玩家已进战后自动出手。
3. 菜单「挂机战斗」选技能时**只记录** `hangupSkillOperate`，不调用 `startHangup`。
4. 未挂机时点 HUD 挂机按钮：先弹技能菜单，确认后本地 `startHangup()`（客户端 `4/1` 遇怪环）；已挂机再点则本地 `stopHangup()`。
5. `[自动]` 出手与经典 CBE 对齐：gateway 记技能 + poll 合成；客户端点「关闭」或手动操作后再停。

## 契约

| 开关 | 状态 | 遇怪 | 战内出手 |
|------|------|------|----------|
| `[自动]` | `autoBattleActive` + gateway `prefer` | 否 | poll `auto_choose_operate`（`LastOperate`） |
| 挂机 | `hangupActive`（客户端遇怪环） | 是（本地定时 `4/1`） | 仅挂机、未开自动时客户端 `4/2` |
| 二者同时 | 均可为真 | 挂机环仍遇怪 | 以 gateway prefer 为准（不开客户端 `4/2`） |

### Gateway `1/4/11`

| 方向 | 字段 | 含义 |
|------|------|------|
| 开 | `type=1` + `Operate` + `hangup=0` | prefer + HangupStyleFlagOk + arm pending；种 `LastOperate`；**不**武装 HangupLoop |
| 关 | `type=0` | prefer=0，停 poll 合成；清 PreferNoHangup |
| CBE | 仅 `type` | 省略 `hangup` → 组队仍可按原逻辑武装 HangupLoop |

服务端标志 `g_mockBattleAutoPreferNoHangup`：Pixi `hangup=0` 置位，避免跨场 / 组队把 `[自动]` 变成挂机遇怪。

## 修改点

- `protocolExtras.buildBattleAutoFlagRequest`
- `GameSession.setBattleAutoPrefer` / `shouldClientDriveInBattleAuto`
- `main.ts`：开/关自动走 `4/11`；`fireInBattleAuto` / `armInBattleAuto` 仅挂机单开
- `mock_server_battle.c`：解析 `Operate` / `hangup`；team prepare / hangup start 尊重 PreferNoHangup

## 验证

1. 手动遇怪 → 菜单开 `[自动]` → 日志 `auto11_seed_operate` + `hangup-style-arm`，随后 `auto-poll` 放同一技能；关自动 / 点关闭 → `type=0 prefer=0`，恢复手动菜单
2. 仅开自动、不开挂机 → 打完一局**不会**自动再遇怪；再遇怪若仍开自动则开战时重发 `4/11 hangup=0` 续 prefer
3. 挂机 alone → 仍客户端 `4/2`；挂机 + 自动 → 出手走 poll，遇怪仍本地 `4/1`
4. CBE 中途按 1（无 hangup 字段）行为不变
