# Pixi UI 字体模糊 / 重影（2026-08-08）

## 结论

PixiJS **没有**专用移动端字体包。清晰路径只有：

1. **DOM 文字**（现用 HTML 左/右软键）— 设备 DPI，不受 canvas pixelate 影响  
2. **点阵 / BitmapText** — 本项目即 CBE `font_gb.uc3`（16×16，`smooth: false`）  
3. 自备 BMFont / MSDF（成本高，且中文全字库大）

系统字体（SimSun / 雅黑）经抗锯齿画进 `image-rendering: pixelated` 的 240×400 画布再放大，必然发糊、加粗、重影 — 与是否「移动端」无关。

## 证据

截图对比：聊天黄字双边/发虚；底部「发送/返回」HTML 软键锐利。

## 修改

- `BitmapLabel` / `makePixelText` 默认 `{ uc3: true, smooth: false }`
- `renderUc3ToRgba` 默认不再 2× 软边（软边 alpha 在 pixelate 下即重影）
- 聊天换行按 `UC3_CELL=16`

## 验证

重装/热更 APK 后打开聊天：字应为硬点阵、无双边晕；行距按 16px 格。HTML 软键仍更「细」，属 DOM vs 点阵风格差，不是同一渲染路径。
