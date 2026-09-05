# WorldHud Texture chrome 黑屏恢复不全

日期：2026-09-01

相关：`2026-08-30-pixi-battle-black-recover-incomplete.md`、`2026-09-01-pixi-disable-cosmetic-atlas.md`

## 现象

去掉翅/坐骑/动态称号 atlas 后仍黑屏；恢复后地图/部分字回来，但：

- 顶栏 UIsh 快捷图标、血槽底图、EXP/等级条
- 软键 GIF（菜单/挂机底）
- 称号 GIF 名条

仍可能空白（假活 Texture）。

## 根因

| 项 | 内容 |
|----|------|
| 触发 | WebView `contextrestored` / stall / alphaMode strip |
| 违反契约 | `UiAssets` canvas Texture 与 BufferImageSource 一样，仅 `source.update()` 不够 |
| 首个错误 | `WorldHud.nudgeAfterGpuResume` 只 remount Graphics 色带，不重建 GIF Sprite |
| 证据 | 截图标红 HUD/EXP；代码 nudge 无 icons/expHost/softArt |

## 修改

| 点 | 行为 |
|----|------|
| `UiAssets.invalidateUiGpuTextures` | 清 full/crop GPU 缓存，保留 rgbaCache |
| `WorldHud.remountTextureChrome` | 重建 tiao/图标/EXP/软键/梦境计时 |
| `main` recover / alpha strip | invalidate 后再 view+hud nudge |
| `SceneView` | remount 后重跑 `setPlayerNameplate` |
| `ActorLayer` | 清空 `titleGifKey` 并刷新称号徽章 |

## 验证

1. 城镇挂机黑屏恢复：顶栏图标、EXP、挂机底、聊天条数字完整。
2. 名条称号 GIF（非动态 atlas）恢复可见。
3. `npx tsc --noEmit`。
