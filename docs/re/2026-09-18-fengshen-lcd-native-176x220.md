# 2026-09-18 封神演义画面只占窗口左上角（未适配游戏帧）

## 现象

`bin/client_out.txt` 已打印：

```text
[info][lcd] game_frame_blit prefer=2 draw=2 view=240x400 surface=480x832 dst=(0,32 480x800)
```

但截图里游戏内容仍只在左上角一块，右侧与下方大片纯黑。用户反馈「只放大了模拟器画面没有适配游戏画面」。

## 证据

对同会话截图做非黑列统计（1× 窗口 `240×432`）：

| 区域 | 结果 |
|------|------|
| 列 0..175 | 有实质像素（场景 + 底栏软键） |
| 列 176..239 | 几乎全黑 |
| 行（去 toolbar） | 实质内容约到 game-y≈219 |

即 guest 只画了 **176×220**（经典 MTK 分辨率），不是满屏 240×400。  
2× 后可见岛为 **352×440**，落在 480×800 的左上，与截图一致。  
因此 `draw=2 dst=480x800` 的日志是对的：宿主确实在 2× 铺满 **空的 240×400 缓冲**，放大的是黑边。

## 根因

首次偏离在 **分辨率契约**：封神演义V2 的设计帧是 176×220，宿主却按江湖 OL 的 240×400 建窗并整缓冲缩放，没有按实际游戏帧裁剪/建窗。

## 修改

| 位置 | 改动 |
|------|------|
| `src/lcd.c` / `lcd.h` | `LcdSetNativeSize`；窗口/present/鼠标按 native 尺寸；只 blit `nativeW×nativeH` |
| `src/main.c` | 路径含「封神」时 `176×220` 且默认 `scale=3`；`--lcd-size=` / `CBE_LCD_SIZE`；`screenImageStruct` 写 native 宽高 |
| 启动器切 CBE | `vm_cbe_load_and_start` 内重新 `LcdSetNativeSize` / `LcdSetScale(3)` |

## 验证

```text
cd bin
main.exe --cbe="CBE/封神演义V2.CBE" --no-launcher
```

期望：`scale=3`、`window=528x692`、`dst=(0,32 528x660)`。`--scale=` / `CBE_LCD_SCALE` 仍可覆盖。
