# 黑屏恢复后跳回开始界面

日期：2026-08-29

相关：`2026-08-29-pixi-blank-alive-alphamode-recover.md`、`2026-08-19-pixi-apk-black-recover-freeze.md`

## 现象

LCD 黑屏经 recover / strip 后画面已能出来（或即将出来），随后整页回到标题/开始界面，需重新登录选角。

## 第一次偏离

不在网关踢号，而在 **软重载契约过宽**：

1. `softReloadGl` → `location.reload()` → 冷启动 `showPhase("splash")`（会话丢失）。
2. 08-29 BLANK_ALIVE 修补把 `stallBroken`（`lastOkRenderAt` 超过 4.5s）直接并入 recover 的 `stillBroken`；alphaMode 吞帧期间 `lastOkRenderAt` 本就陈旧，**即便本趟 strip/render 已能画图也会立刻 soft reload**。
3. `GL_SOFT_RELOAD_AFTER = 1`：一次假 blank 即重载。
4. `alphaModeProbeBlockedUntil` 窗口内 `regionLooksBlank` 曾固定返回 `true`，把「探针不可用」当成「仍黑」→ 再推一把 reload。

权威兜底仍应是「确认仍黑再 reload」；BLANK_ALIVE 的持续吞帧已由 `softReloadGl("alphaMode-storm")` 单独处理，不必在 recover 里用 stall 再砍一刀。

## 修改

| 点 | 行为 |
|----|------|
| recover `stillBroken` | 仅 `isGlContextLost()` 或 blank 探针 **明确 true**；去掉 stallBroken 旁路 |
| blank 探针 | 探测窗口 / extract 失败 → `null`（不计入 soft-reload） |
| strip 本趟 | `stripped > 0` 时不 reload，并回退一次 consecutive 计数 |
| `GL_SOFT_RELOAD_AFTER` | `1` → `2` |
| alphaMode-storm reload | strip 后再等一整段 `ALPHA_RELOAD_AFTER_MS` 仍无 ok render 才 reload |

## 验证

1. 城镇黑屏后：应见 recover / strip / recover-ok，**不应**立刻 `[gl] soft reload` 回标题。
2. 真·持续 BLANK_ALIVE（strip 后仍每帧 alphaMode）：数秒后仍可 `alphaMode-storm` reload（预期会丢会话）。
3. `webglcontextlost` 假恢复仍 blank：连续两次 failed-recover 后才 soft reload。
