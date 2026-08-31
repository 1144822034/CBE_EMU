# Pixi 黑屏恢复后聊天面板底框未恢复

日期：2026-08-31

相关：`2026-08-30-pixi-battle-black-recover-incomplete.md`、`2026-08-18-pixi-apk-black-screen-plan.md`

## 现象

城镇打开聊天（软键「发送 / 返回」）后遭遇黑屏「恢复」：

- 顶栏 / 血槽 / 挂机钮 / 地图与角色已可见
- 世界频道 `[世]` 文字仍在 LCD 中部
- **聊天面板绿框 / 深色底 / 半透明遮罩全无** → 字直接叠在地图上
- 会话仍在线；`phase=chat` 软键正确

## 第一次偏离

不在聊天协议或 `showPhase`，而在 **GL 恢复契约漏了打开中的 overlay 面板**：

1. `recoverDisplayAfterForeground` / alphaMode strip / late 280ms 只 nudge `battle` 或 `view`+`hud`。
2. `refreshAllUc3Labels` 把 `ChatPanel` 的 BitmapLabel 字救回。
3. `ChatPanel` 的 `shade` / `box` / `inputHit` 是 Pixi `Graphics`；restore 后 GPU 几何空白，仅 `clear()+paintCbePanel` 不够（既有 Graphics 失效契约）。
4. `syncInputAfterGlRecover` 按 `phase=chat` 重设软键 → 「发送/返回」在、面板底框不在。

## 根因陈述

| 项 | 内容 |
|----|------|
| 触发 | `webglcontextlost` → `contextrestored`（或 stall recover）时聊天面板正打开 |
| 违反契约 | 仍挂在 stage 上的可见 overlay Graphics 须 `replaceGraphics` 后重画 |
| 首个错误状态 | 面板 chrome 空白；UC3 消息行仍画在原坐标 |
| 证据 | 截图：字+世界场景+发送/返回；代码 recover 无 `chatPanel.nudge*` |

## 修改

| 点 | 行为 |
|----|------|
| `ChatPanel.nudgeAfterGpuResume` | `replaceGraphics(shade/box/inputHit)` → 重画 shade → `redraw()` → IME 布局 |
| `main.nudgeOverlayPanelsAfterGpuResume` | 可见时调用；挂到 recover / late 280ms / alphaMode strip |

## 验证

1. 打开聊天后复现黑屏恢复：面板深色底与频道条应回来，字不再裸叠地图。
2. 关闭聊天后恢复：不应误开面板。
3. 城镇 / 战斗无面板路径：与既有 recover 行为一致。

## 未决

其它 `paintCbePanel` overlay（菜单 / 背包 / 对话等）仍可能同类缺口；本次仅修截图路径中的 `ChatPanel`。
