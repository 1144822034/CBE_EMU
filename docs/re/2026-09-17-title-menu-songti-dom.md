# 标题菜单：系统宋体走 DOM，点阵仍用 title2.gif（2026-09-17）

## 行为

| 字体偏好 | 开始界面四行菜单 |
|----------|------------------|
| `cn`（高清/系统宋体） | `BitmapLabel` DOM 叠字：16px + 描边；选中 `menuHi`/`menuHiStroke`，未选中白字黑边 |
| `sky` / `gb16` / `gb` | 原 `title2.gif` 2×4 格裁切 |

字体切换后 `title.refreshAfterFontChange()` 重画菜单（从世界回标题也会在 `finishIntro` 再 `redrawMenu`）。

## 修改

- `TitleScreen.redrawMenu`：`preferSystemFont()` 时跳过 GIF
- `main` action 270：切字体后调用 `refreshAfterFontChange`
