# 2026-09-18 原罪天使对话框无文字（font 写屏 pitch）

## 现象

`176×220` native + pitch 修复后：场景与底栏正常，「菜单 / 商店 / 500 / 20」可见，但中间对话黑框**空白无字**。

## 根因

`UpdateLcd` / `DrawImage*` 已按 `LcdGetScreenPitchPixels()`（176 对齐）读写 `Lcd_Cache`；`fontEngine.c` 的 `drawFontChar*` 仍用容量常量：

- 裁剪：`px >= LCD_WIDTH`（240）
- 写点：`offset = py * LCD_WIDTH + px`

对话 `DrawString` → UC3 字形写到 **pitch=240** 地址，present 按 **pitch=176** 取样 → 字形落在对话框外的错位行（框仍在，字看不见）。软键汉字多为 UI 位图，不经字库，故仍正常。

## 修改

| 位置 | 改动 |
|------|------|
| `src/fontEngine.c` `drawFontChar` / `drawFontCharWithWidth` | 裁剪用 `LcdGetNativeWidth/Height`；写缓存用 `LcdGetScreenPitchPixels()` |

## 验证

```text
cd bin
main.exe --cbe="CBE/原罪天使(大型RPG)V100.CBE" --no-launcher
```

期望：对话黑框内出现剧情/提示文字；软键与场景仍正常。
