# 地图按 OK 自动遇怪

日期：2026-08-08

## 触发与现象

大地图按 D-pad OK（或键盘 F/Enter）时，即使旁边没有 NPC，也会立刻进入战斗。

## 根因

`handleOk` 在 `phase==="world"` 且无邻近 NPC 时调用
`session.startBattleNearPlayer(x,y)`（无 prefer id）。该函数会：

1. 在整张图 `sceneMonsters` / catalog 里找最近怪开战，或
2. fallback `id=9001` 野战

这是早期调试用「F 遇怪」捷径，不是 CBE 地图 OK 契约。软键「菜单」本来就不会开战；碰撞/点怪/挂机才是遇怪入口。

## 修改

世界相位 OK：仅 `approach.confirm` 或打开邻近 NPC 对话提示；否则提示「附近没有可对话对象」，**不**发 `4/1`。

## 验证

1. 野外空地按 OK：不进战，状态栏提示无可对话对象。
2. 靠近 NPC 按 OK：仍出接近/对话。
3. 撞怪 / 点怪 / 挂机：仍可开战。
