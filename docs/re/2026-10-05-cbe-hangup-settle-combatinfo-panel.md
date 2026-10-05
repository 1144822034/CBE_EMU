# CBE 挂机结算误走手动战斗结算

日期：2026-10-05

## 触发与现象

挂机胜利后普通结算带 fdata「[挂机中]」，不是原版 `sub_7794` 挂机累计壳。
模拟器冷投 `8996` 会 `UC_ERR_MAP`。

Pixi HUD 挂机仍走 `4/1` 碰怪，不经过 `2/10 Type=2` 首包。

## 根因

`sub_7794` 需要 `game+1136=1`，仅 `Handle25_2` 在客户端自己的 `25/2` wait 上写入。
pay 原先把首个 `2/10 Type=2+25/3` 直接开战，CBE 从未进入该 wait。

## 修改

1. **服务端三阶段**（对齐 CBE_EMU）：
   - 未挂机：`2/10 Type=2+25/3` → `2/10` 空 + `25/3` 确认面板
   - 独立 `25/2(type=1)` → 置 `sceneHangupEnabled`，回 `25/2 result=1`
   - 之后 `2/10 Type=2` 或客户端 `4/1` 碰怪开战
   - 胜利 `4/7` 带 51 字节 `combatinfo`
2. **CBE 模拟器**：地图点挂机 **不转发** `25/3`、**不**在 `on_send` 里
   `bind_hangup_confirm_mmbattle_r9`。
   client_out 2026-10-05：转发后立刻 `hangup_confirm_bind_r9 code=0501ed60
   r9=05032d60`，随后 `pc=0502da7a`（mmGame ScreenLogic）`r0=6a8e3180`
   `UC_ERR_MAP` / `SCR_Event`。该模块是户外 mmGame，不是 mmBattle；改写
   `g_currentScreenModuleBase` 是本次闪退的第一次偏离。
   稳定开战：本地 `2/10+25/12` dismiss「获取数据」；host 上行 `25/2(type=1)`
   置 `sceneHangupEnabled`，再 replay `Type=2` 走 `hangup_battle_start`。
3. Pixi `startHangup` 仍是 `4/1`，不弹 25/3。

## 验证

1. 重启服务端 + CBE。点挂机：`forwarded=0` `map-send-no-bind-r9-no-25/3`、
   `cbe_hangup_uplink_25_2`、`cbe_hangup_replay_start`，随后开战。无
   `SCR_Event` / `hangup_confirm_bind_r9`。
2. 结算：`mock_scene_hangup_combatinfo`（若已 enabled）。原生 `sub_7794` 壳仍
   unresolved（`game+1136` 需 guest 自己的 `25/2` wait）。
3. Pixi「斗」立刻碰怪，无第二层确认。
4. 停挂机只认结算「停止挂机」`2/10+25/2(type=2)`；主界面二次点挂机不记停。
   场间已在地图时该包必须清环并提示「已停止挂机」，不得再 poll 开战
   （见 `2026-10-05-cbe-hangup-settle-stop-still-starts.md`）。
5. CBE 挂机首场 4/7「挂机中」后 **skip-4/8** 留在结算壳；场间后再
   prepend `4/8`+`4/5`（不开 `4/11`，避免无 `game+1136` 时「自动战斗中」）。
   结算期间空 `25/5` 不得回 `result=4`（见 `2026-10-05-cbe-hangup-settle-second-fight-div0.md`）。
   Pixi `4/1` 仍 skip-4/8。
6. CBE 点挂机先弹宿主技能菜单（普通攻击 / `skill.dsh` 技能名 / 取消），确认后
   `25/2` + Type=2 带 `Operate` 开战。scene hangup 开战包不下发 `4/11`，避免
   无 `game+1136` 时卡在「自动战斗中」不播 4/6；出手走 poll synth + 所选挂机技能。

## unresolved

户外 mmGame 上不能把当前屏推断成 mmBattle 再绑 R9 / 冷投 `8996`。
`game+1136` 与原生挂机确认壳、挂机结算「停止挂机」按钮仍未闭环。
