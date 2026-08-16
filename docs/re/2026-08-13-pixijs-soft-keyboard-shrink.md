# PixiJS 软键盘挤缩游戏画面

日期：2026-08-13

## 现象

不同高度手机上，弹出系统软键盘后 240×400 LCD 被重新 `fit` 得更小；矮屏上 HTML `#touch-bar`（方向键）本身也占掉大量视口。

## 根因

1. `imeShell` 会给 `html` / `#app` 加 `ime-open`，并打算锁定 `#phone-wrap` 尺寸，但 **缺少对应 CSS**（未隐藏 `#touch-bar`、未钉住布局高度）。
2. `#app` 使用 `100dvh`；Android WebView 默认 `adjustResize` 时键盘会缩小布局视口 → `ResizeObserver(#stage)` → `fitPhoneWrap` 按更矮的 stage 缩放。
3. 激活 IME 时若先清 lock 再按已缩小的 stage 采样，会把「缩小后的尺寸」写成 lock。
4. 矮屏上 D-pad 约 180–220px，stage 过矮，即使无键盘 LCD 也偏小。

## 修复

| 层 | 改动 |
|----|------|
| `imeShell.ts` | 激活时先快照 wrap + `--ime-layout-h`；`adoptLock` **只升不降** |
| `index.html` | `.ime-open` 隐藏 `#touch-bar`、固定 `#app` 高度；`max-height:720/600` 压缩软键栏 |
| `main.ts` | IME 激活：暂清 lock → 按放大后的 stage fit → capture，再与 floor merge |
| `AndroidManifest` | `windowSoftInputMode="adjustPan"`，避免 WebView 被键盘顶矮 |

## 验证

- 登录页点账号/密码：LCD 尺寸不变，底栏暂时隐藏，输入框仍可点中。
- 聊天/交易金额/创角名：同上；收起键盘后底栏恢复，LCD 回到原缩放。
- 矮屏（≤720px 高）无键盘时：方向键更小，LCD 更大。
- 横竖屏切换后重新 fit 正常。

## 方向键挤占（续）

固定 `max-height` 媒体查询不够细。改为 `syncTouchChromeScale()`：

- 按视口**宽度**定首选键位：`cell ≈ vw * 56/360`，大屏键大、小屏键小
- 再按「铺满宽度」的 LCD 高度 `vw * 400/240` 算剩余高度预算；底栏不超过视口约 42%
- 预算不够则缩小 `--pad-scale`，过挤时隐藏 `#touch-status`
- 预算充裕时可略放大；软键侧栏 `--sk-col-min/max` 同样跟宽度走

CSS 用 `calc(base * var(--pad-scale))`，不再靠 720px 断点硬砍。

## 矮屏键位过小（2026-08-14）

### 现象

部分机型（约 360×640–720、iPhone SE、宽矮安卓）方向键仍约 32px，难点。

### 根因

旧公式 `padBudget = min(vh - lcdByWidth, vh*0.4)`：**先按 LCD 铺满宽度占高**，剩余不足时把 `--pad-scale` 压到 `CELL_MIN=32`。矮屏 leftover 常只有 40–120px，三行 D-pad 必然缩到下限。`padBudget≈0` 时甚至不缩放，宽屏/平板反而异常。

### 修复

- 预算改用 `#app` **内容盒**（扣除 safe-area padding）
- `CELL_MIN` 48、`CELL_MAX` 80；宽度首选也不低于 48
- **先保证** `minChrome = chromeH(48, false)`，不够则让 LCD 在 stage 内略缩（`fitPhoneWrap` 本就会按 stage 等比缩放）
- 底栏仍封顶约 42% / 不超过 52% 视口，避免吃掉画面

### 验证

- 360×640 / 360×720 / iPhone SE：单键 ≥48px，LCD 仍可读（宽度略小于全宽属预期）
- 360×800+ 高屏：行为与改前接近，键随宽度走
- 隐藏方向键 / IME compact 行：仍为半高软键条
