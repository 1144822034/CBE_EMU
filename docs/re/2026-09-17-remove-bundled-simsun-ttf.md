# 移除打包 simsun.ttf，cn 改用系统宋体（2026-09-17）

## 结论

`public/simsun.ttf`（约 10MB）不再打进 APK / dist。高清字体（`cn`）直接走 OS 宋体栈：

`SimSun, "宋体", "Songti SC", "Noto Serif CJK SC", …`

无系统宋体的设备本来就显示异常，打包 TTF 也救不了，只白白涨包体。

## 修改

- 删除 `PixiJS_web/public/simsun.ttf`（及 dist 副本）；`.gitignore` 加 `public/simsun.ttf`
- `systemFont.ts`：去掉 FontFace / Assets 加载；`applyUiFontId("cn")` 只切 CSS
- `uiFontPreference.ts`：`cn.url=""`、`file=系统宋体`
- `index.html` / `AGENTS.md` 同步说明

## 验证

1. `public/` 与出包产物无 `simsun.ttf`
2. 有宋体的桌面/部分 ROM：切「高清字体」DOM 叠字正常
3. 无宋体设备：用天空点阵 / 全字库，不依赖打包 TTF
