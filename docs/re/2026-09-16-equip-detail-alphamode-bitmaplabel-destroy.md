# 装备详情 alphaMode 黑屏（BitmapLabel destroy / CN Medium）

日期：2026-09-16（续）

相关：`2026-08-29-pixi-blank-alive-alphamode-recover.md`、字体切换 `cn`（CN-MEDIUM.OTF soft-AA）

## 现象

`phase=system` 打开背包装备详情后：

- `RENDER_CRASH why=render-crash:alphaMode:renderer`
- 连续 `swallow` / `stripped broken textures`（常为 12→36 循环）
- `recover display interval-stall` 无法脱出

## 根因（两段）

### A. 首次偏离（触发 strip）

装备详情大量重建 `BitmapLabel`；Pixi v8 默认 `destroy()` 不销毁子 Sprite；`cn` soft-AA 若走共享 `crispTexCache`，重绘/evict 后易留下死 `BufferImageSource`。

### B. 恢复循环（本次主因）

`onAlphaModeSwallow` / `recoverDisplayAfterForeground` 调用 `invalidateUiGpuTextures()` 会 `destroy` `fullCache`/`cropCache` 里的 Texture，但：

| 次级持有者 | 问题 |
|------------|------|
| `cbePanelCorners.cornerTex` | `flowerReady` 仍为 true，nudge→`paintCbePanel` 再次挂上**已销毁**的花角贴图 |
| `ItemArt.iconCache` / `frameTex` | 穿戴/替换图标死源 |
| `SystemPanel.moneyTex` | 金币图标死源 |

于是 strip → invalidate → nudge/redraw 挂回死贴图 → 再 alphaMode → 循环。`recover stripped broken textures 36` 即此。

## 修改

| 点 | 行为 |
|----|------|
| `invalidateCbeFlowerCornerGpu` | 清空 `cornerTex`，`flowerReady=false`，重新 warm |
| `invalidateEquipArtGpu` | 清空 icon/frame 缓存 |
| `main` invalidate 后 | 必调上述两者，再 `nudgeOverlayPanels` |
| `SystemPanel.nudgeAfterGpuResume` | 丢弃 `moneyTex` / equipArt / actor，再 redraw |
| `cn` soft-AA | 标签**独享** bake 贴图，destroy 时 `destroy(true)`，不进共享 cache |
| `BitmapLabel.destroy` | 解绑 + `children:true` 且不销毁共享 UC3 bake |

## 验证

1. 思源中黑 → 打开装备详情：不应再进入 strip/recover 循环。
2. 详情内翻页/点操作：无持续 `storm=60`。
3. 故意触发一次 strip 后：花角可暂时矢量，随后 GIF 重载，不应反复 36 strip。
