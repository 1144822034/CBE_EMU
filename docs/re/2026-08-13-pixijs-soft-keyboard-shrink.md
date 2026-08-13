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

- 按视口宽算出「铺满宽度」的 LCD 高度 `vw * 400/240`
- 剩余高度作为底栏预算，写入 `#touch-bar { --pad-scale }`
- 下限 0.55；且底栏不超过视口 40%
- `pad-compact` 时隐藏 `#touch-status` 再省一行

CSS 全部用 `calc(base * var(--pad-scale))`，不再靠 720/600 断点硬砍。
