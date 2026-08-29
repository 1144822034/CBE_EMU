# Pixi BLANK_ALIVE：alphaMode 吞帧后无法恢复

日期：2026-08-29

相关：`2026-08-22-pixi-render-crash-cosmetics-plist.md`、`2026-08-19-pixi-apk-black-recover-freeze.md`

## 现象（本次取证）

城镇 `phase=world`，`peers≈32` / `peersWithCos≈7`：

- 每帧 `[gl] swallow render alphaMode (renderer, frame skipped)`
- `RENDER_CRASH why=render-crash:alphaMode:renderer blank=true lost=false`
- `RENDER_STALL` 达数万 ms；heartbeat `BLANK_ALIVE`
- **无法自动恢复**（会话仍在，LCD 一直黑）

`blank` 探针经 `extract.pixels` → `generateTexture` → 再次撞上 alphaMode。

## 第一次偏离

不在视口 / contextlost，而在 **死贴图 + recover 契约**：

1. 妆容（或其它）`texture.source` 已 null，Batcher 读 `alphaMode` 抛错。
2. `main` 吞掉该帧（ticker 不停）→ `lastOkRenderAt` 永不更新。
3. recover / interval **只**在 `glLostAt>0` 或 context 事件时探针 blank / soft-reload。
4. `lost=false` → 亮屏 recover 走 `visibility`/`focus` 时 `probeBlank=false` → 误判 recover-ok → **永不 reload**。

对照：08-22 文档预期「偶发 swallow 后继续动」；实际是 **持续 swallow** 且无第二档恢复。

## 修改

| 点 | 行为 |
|----|------|
| `stripBrokenTextureSprites` | 隐藏 source 已死的显示对象，让下一帧能画地图 |
| alphaMode storm | ≥24 帧吞掉后 strip + nudge；≥4.5s 无 ok render → `softReloadGl("alphaMode-storm")` |
| blank 探针 | storm 窗口内跳过 `extract.pixels`（避免二次崩溃） |
| recover | `visibility`/`focus`/`interval-stall` 也探针；stall 过久直接 stillBroken → reload |
| interval | `lost=false` 但 storm/stall 时走 `interval-stall` recover |

## 验证

1. 复现黑屏后：数秒内应见 `[gl] stripped broken textures` 或 `[gl] soft reload alphaMode-storm`，不应无限 swallow。
2. reload 后城镇可玩；若 strip 成功可不 reload 即出图。
3. 正常无死贴图路径：不应误 reload。
