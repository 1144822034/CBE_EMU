# 2026-09-18 原罪天使横纹花屏（screen pitch 176/240 混用）

## 现象

裁到 `176×220×3` 后右侧垃圾列消失，但场景区仍有明显横纹雪花；软键文字正常。

## 根因

`screenImageStruct` 已写成 native `176×220`，guest 按 **pitch=176** 写屏；宿主 `DrawImage*` 在画到 screen 时仍强制 `dst_w=LCD_WIDTH(240)`，`UpdateLcd` 也按 **pitch=240** 读 `Lcd_Cache`。  
同一缓冲两种行跨度混用 → 行错位，表现为横纹花屏。

## 修改

| 位置 | 改动 |
|------|------|
| `lcd.c` | `LcdGetScreenPitchPixels()`；present / Android 按 native pitch 取样 |
| `vmFunc.c` | screen 目标用 `LcdGetNativeWidth/Height`，cache 同步条件对齐 |
| `main.c` | frame copy、fill/sync、softkey、mouse clamp、LCD mgr 画线/填色按 native pitch |

江湖 OL 仍为 `240×400`，pitch 不变。

## 验证

```text
main.exe --cbe="CBE/原罪天使(大型RPG)V100.CBE" --no-launcher
```

期望：场景无横纹错位，软键/对话清晰，窗口 `528×692`。
