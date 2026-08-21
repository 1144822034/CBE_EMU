# APK 黑屏恢复后假死 / 不自动恢复

日期：2026-08-19

相关：`2026-08-17-pixi-apk-cosmetics-black-screen.md`、`2026-08-18-pixi-apk-black-screen-plan.md`

## 现象

黑屏之后：

1. **有时不自动恢复**（一直黑，或假「好了」却仍黑）。
2. **有时画面回来但卡住**：方向键 / 软键全无响应。

## 根因（相对此前 recover）

| 问题 | 机制 |
|------|------|
| 假成功取消 reload | `canvasLooksBlank` 只采 HUD `(16,6,80,20)`；`CBE.bgDeep` 深色仍算「有像素」→ `glLostAt` 被清、watchdog 取消，真正地图带仍黑 |
| 主线程卡死 | `setInterval` 在 still-lost 时每 2s 全量 `refreshAllUc3Labels` + `extract.pixels`，按键事件排不上 |
| `recoverInFlight` 挂死 | UC3 重烤永不返回 → 后续 recover 全部 no-op |
| 输入状态残留 | `webglcontextlost` 未 `touch.releaseAll` / `clearHeldKeys`；软键 `setPointerCapture` 卡住吞掉后续 tap；恢复后未重绑 softkeys / `events.setTargetElement` |

## 修改

- 空白探针改采地图带 `(20,90,120,120)`；登录等深色标题屏跳过探针。
- lost 期间 interval **只** `restoreContext`；重恢复交给 `contextrestored` / blank 探针。
- recover 8s hang → soft reload；UC3 rebake 4s timeout。
- 假恢复一次即 soft reload（`GL_SOFT_RELOAD_AFTER = 1`）。
- lost / hide / recover 后：`releaseAll`（释放 pointer capture）+ `syncInputAfterGlRecover`。

## 验证

- 复现黑屏后：应在数秒内 `contextrestored` 或 soft reload，不应长期假黑。
- 恢复后：世界/战斗软键与 D-pad 可点；挂机空软键时 OK 仍可用。
- 长时间挂机 + 切前后台，观察是否再出现「画面在、键全死」。
