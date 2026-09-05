# 临时关闭地图翅/骑/动态称号 atlas（防黑屏）

日期：2026-09-01

## 目的

TexturePacker 翅 / 坐骑 / 动态称号图集占用 WebView VRAM，易触发黑屏。临时：

- **不渲染** atlas（自己与附近玩家）
- **保留** 装备 ID、翅膀/坐骑面板、服务端功效
- 动态称号改为 **文字名** 回落（`chenghaoLabel`）
- 资源挪到 `PixiJS_web/assets.bak_cosmetics/`，sync/APK **不打包**

## 开关

`PixiJS_web/src/resources/mapCosmeticAtlas.ts` → `MAP_COSMETIC_ATLAS_ENABLED = false`

## 恢复

见 `PixiJS_web/assets.bak_cosmetics/README.md`。
