# CBE 模拟器挂机自动战斗节奏

日期：2026-10-04

## 触发与现象

CBE 模拟器点挂机（`2/10 Type=2`）后能连打，但体感空等明显：

1. 出手后要停好几秒才下一刀。
2. 结算面板起很久才进下一场。
3. 挂机累计（`4/7.combatinfo`）有时每场从 0 再计。

运行日志（旧进程，`CBE_BATTLE_AUTO_TURN_GAP_MS=5000` / 场间 8000）还叠加了 5s 取消窗；当前源码默认 gap=0、场间 2000。即使去掉这两项，挂机环仍走 Pixi 播报 hold。

## 预期 / 实际

- 预期：CBE mmBattle 约 100ms/帧播完 actioninfo 即可下一刀；结算看一眼 combatinfo 后按挂机环再开战；累计跨场累加。
- 实际：服务端 `playback_hold` 默认 **1400ms × 动作数**（2 动 ≈ 2.8s，再加旧 5s gap 就是日志里的 7800）；结算窗默认 **2500ms**；离场 `4/8+4/11 type=0` 被 CBE 下行 shim 当成关自动，清掉 combatinfo 累计。

## 根因

1. **播报/结算时钟不区分客户端**：Pixi `[自动]` / 客户端 `4/1` 挂机需要长 hold，避免 `4/8` 砸未播完的 BattleScreen。CBE HangupLoop 共用同一套 1400/2500。
2. **累计清零**：`network-client.c` 见任意 `4/11 type=0` 就 `cbe_hangup_settle_ui_clear`。挂机离场包里的 type=0 是 tear-down，不是玩家关自动。

Pixi 挂机仍是本地 `4/1` 环、`hangup=0` 不武装 HangupLoop，不走本次短节拍。

## 修改

| 位置 | 改动 |
|------|------|
| `mock_server_battle.c` | HangupLoop 武装时：动作 hold 默认 **600ms**（`CBE_HANGUP_AUTO_ACTION_PLAYBACK_MS`），结算窗 **1200ms**（`CBE_HANGUP_SETTLEMENT_EXIT_DELAY_MS`）。日志 `hangup_pace=1`。 |
| `network-client.c`（桌面 + JNI 副本） | 同包带 `4/8` 的 `4/11 type=0` 不清累计；单独 `4/11 type=0` 或聊天「已停止挂机」仍清。 |

服务端挂机再入仍可 skip `4/11 type=0`（既有 `hangup-skip-4/8`）；本改兜住仍下发 `4/8+type0` 的路径。

## 验证

1. CBE 野外点挂机：`mock_battle_playback_hold ... hangup_pace=1 hold_ms` 约为 `600×动作数`（无 `TURN_GAP` 时 2 动 ≈ 1200，不是 7800）。
2. 胜后 `settlement_exit_arm panel_ms=1200`，约 2s 地图等待后再 `hangup_loop_poll_deliver`。
3. 连续多场：`cbe_hangup_combatinfo tally` 递增，不应每场 `tally=1/64`。
4. 战中关自动 / 点挂机停：累计停止，不再遇怪。
5. Pixi 仅 `[自动]`：`hangup_pace=0`，hold 仍约 1400ms/动作。
6. `make -j2` 通过。
