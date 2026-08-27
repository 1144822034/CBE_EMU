# 黑屏恢复后画面向左上偏移

日期：2026-08-25

## 现象

APK / WebView / Electron 游戏画面黑屏后再恢复，LCD **整体向左上方位移**：

- 左侧 / 顶边内容被裁切
- 右侧 / 下方相对多出舞台黑边
- 右上角 HTML 白点（`#touch-chrome-toggle`）原先钉在 `#stage` 上，看起来像「挡住」本应在右上角的画面

截图特征：底栏 D-pad 仍相对窗口居中，仅 `#phone-wrap` / LCD 偏左上。

## 触发与链路

1. 息屏 / 切后台 / `webglcontextlost` → LCD 黑或空。
2. 亮屏 → `visibilitychange` / `focus` / `contextrestored` → `recoverDisplayAfterForeground`。
3. 第一轮修补只清 scroll / `transform=""` 并 `fitPhoneWrap`。
4. 仍复现：Chromium 在 GL restore 后可能把带 transform 的层留在旧偏移上；把 centering 的 transform 清成 `""` 依赖 flex 居中时，合成层偶发不归位。

Manifest：`android:windowSoftInputMode="adjustPan"`。`imeShell` 键盘打开时按 `visualViewport.offsetLeft/Top` 写补偿 translate。

## 第一次偏离

不在地图相机 / 协议，而在 **外壳视口几何 + 合成层**：

- 预期：恢复后 `#phone-wrap` 稳定居中填满 `#stage`（或等比居中），toggle 贴在 LCD 右上角。
- 实际：残留 pan / 错误 transform / 合成层偏移把 LCD 顶向左上；stage 黑边露在右侧；钉在 stage 上的白点落在黑边上，像「右上角挡住画面」。
- GL 贴图恢复成功（画面已“回来”）会掩盖这一布局契约破坏。

## 修改

| 点 | 行为 |
|----|------|
| `#phone-wrap` 居中 | `position:absolute; left/top:50%; transform:translate(-50%,-50%)`（不再靠 flex） |
| `SHELL_WRAP_CENTER_TRANSFORM` | 复位时恢复居中 transform，禁止清成 `""` |
| IME `visualViewport` | `translate(calc(-50% - oxpx), calc(-50% - oypx))` |
| `kickShellCompositor()` | 恢复后 bounce transform + canvas display，逼合成层重绘 |
| `repairShellViewportAlignment()` | reset + kick；`shellViewportSnapshot()` 检测 pad 不对称 |
| `#touch-chrome-toggle` | 移入 `#phone-wrap`，随 LCD 走 |
| `visibility` / `focus` / `refitShellLayout` | 多次复位 + 晚到 280ms 再补；forensic 带 `shell` 快照 |

## 验证

1. Electron / APK：城镇或战斗 → 息屏数秒 → 亮屏：LCD 左右对称贴齐 stage，无整块左上漂移。
2. 右上白点应落在 LCD 右上角（贴角色/地图角），不应孤零零停在右侧黑边里。
3. 打开聊天/登录键盘后关闭，再息屏亮屏：同上。
4. 偶发 `[gl] webglcontextlost` 后恢复：出图且布局不偏；若仍偏，控制台应有 `[gl] shell still misaligned after refit` 与 forensic `shell` 字段。

## unresolved

- 若仍偏移，对照 forensic `shell.padL/padR/padT/padB`、`wrapTransform`、`vvOffsetLeft/Top`。
- 长期是否改为 `adjustResize` 需单独评估（与现有 IME lock 契约相关）。
