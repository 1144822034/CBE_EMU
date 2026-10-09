# 弹窗打开时世界背景不再压暗

日期: 2026-10-09

## 问题

打开背包 / 菜单 / 对话 / 聊天等全屏弹窗时，地图被一层 `alpha≈0.35–0.45` 的黑色 scrim 罩住，视觉像整屏变暗。CBE 面板本身已是不透明青绿底，压暗多余。

## 改动

| 位置 | 说明 |
|------|------|
| `constants.ts` | 新增 `PANEL_WORLD_DIM_ALPHA`（默认 `0`）、`paintPanelWorldDim` / `makePanelWorldDim` |
| 各世界弹窗 | 统一走上述 helper；点击拦截仍靠 `blockPanelPointerPassthrough` |

涉及：`SystemPanel`、`MenuScreen`、`DialogPanel`、`ChatPanel`、`GuildPanel`、`TaskPanel`、`SocialPanel`、`EnhancePanel`、`TradePanel`、`EntertainmentPanel`、`MountPanel`、`WingPanel`、`RoleScreen` 删除确认、`FruitPanel` 投币层。

若以后要轻微模态感，只改 `PANEL_WORLD_DIM_ALPHA`（例如 `0.12`）即可。

## 验证

1. 城镇打开背包 / 属性 / 菜单：地图边缘保持原亮度，不被灰黑罩住。
2. 点面板外（地图挂机热区等）仍不应穿透。
3. GL 恢复后开面板：底框 / 边花正常（remount 路径仍画 scrim Graphics）。
