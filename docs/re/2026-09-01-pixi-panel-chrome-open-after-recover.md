# Pixi 属性/菜单面板底框：恢复后打开仍丢失

日期：2026-09-01

相关：`2026-08-31-pixi-chat-panel-black-recover.md`、`2026-08-31-pixi-black-screen-root-fix.md`

## 现象

城镇 GL 恢复后打开「属性」（软键「左软 / 返回」= `left:""` 回落 + `right:"返回"`）：

- UC3 属性字、称号名条仍在
- **面板深色底 / 边框 / dim 全无** → 字叠在地图与名条上
- HUD 顶栏/底栏色带也可能空白（匿名 `Graphics` 未 remount）

与聊天「打开中恢复」同类，但触发时序不同。

## 第一次偏离

| 步 | 行为 |
|----|------|
| 1 | `webglcontextrestored` / stall recover |
| 2 | `nudgeOverlayPanelsAfterGpuResume` **仅对 `visible` 面板**调用 nudge |
| 3 | `SystemPanel` 等当时隐藏 → 跳过；`box` / dim 仍是 restore 后失效的 Graphics |
| 4 | `refreshAllUc3Labels` 把字救回（全局） |
| 5 | 用户随后 `openSystem("status")` → `open()` → `box.clear()+paintCbePanel` **画在死 Graphics 上** |
| 6 | 软键按 phase 正确；chrome 空白 |

聊天修复只保证「恢复时已打开」路径；**恢复后才打开**仍违反「长寿命 Graphics 须 replace 后再画」。

## 根因陈述

| 项 | 内容 |
|----|------|
| 触发 | GL recover 时 overlay 未打开；之后再开属性/菜单/对话 |
| 违反契约 | 隐藏中的 `paintCbePanel` / dim Graphics 也须 `replaceGraphics`；仅 `visible` nudge 不够 |
| 首个错误状态 | `open()`/`show()`/`showPhase(menu)` 用旧 Graphics 重画 → 底框空白、字仍在 |
| 证据 | 截图：属性字 + 地图透出 + 左软/返回；代码 nudge 早期 `if (!visible) return` |

## 修改

| 点 | 行为 |
|----|------|
| `SystemPanel` | `dim` 字段化；`remountChrome`；`open` 与 nudge 均 remount |
| `ChatPanel` / `DialogPanel` / `MenuScreen` | open/show 或 phase=menu 时 remount；nudge 隐藏也 remount |
| `main.nudgeOverlayPanelsAfterGpuResume` | **始终**调用各面板 nudge（内部再按 visible 决定是否 redraw） |
| 各 overlay `nudgeAfterGpuResume` | 先 `replaceGraphics`，再 `if (visible) redraw` |
| `WorldHud` | 顶/底色带与地点条 Graphics 字段化并在 nudge remount |

## 验证

1. 世界界面触发黑屏恢复后**再**开属性：深色底与标题胶囊应在，字不裸叠地图。
2. 恢复时属性已打开：底框仍在（原路径）。
3. 恢复后开菜单 / 对话 / 聊天：同上。
4. `npx tsc --noEmit`（`PixiJS_web`）。

## 未决

- `BattleScreen` 仍仅在 visible 时 nudge（开战斗有独立路径）。
- 匿名 dim 未字段化的少数面板仍依赖「recover 时全局 remount box」；open 路径若未 remount 仍可能漏。
