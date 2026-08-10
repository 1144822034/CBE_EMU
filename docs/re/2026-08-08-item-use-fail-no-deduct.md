# 2026-08-08 背包使用失败不扣除

## 触发与首次偏离

背包对无效果或不可消耗道具发通用 `1/7/1` 时，mock 曾在扣栈后仍回
`7/1 result=1`（或失败路径仍让客户端 `openBag`/合并库存增量）。  
CBE `HandleItemOperationResponse`（`JianghuOL.CBE:0x01033544`）在
`result=1` 时会本地扣数量；失败契约应为 `result=0` + 提示，且不带
`7/7`/`7/11` 库存变更。

## 根因

1. **服务端**：无 HP/MP/EXP（且非壶/扩容/小喇叭）仍 `consume` 并回成功。  
2. **客户端**：`useItem` 无条件 `handleBodies`，UI 无条件 `openBag`，失败时也会按响应/库同步掉数量。

## 修改

- `vm_net_mock_build_item_use_response`：无效果或 consume 失败 →  
  `7/1 result=0` + `16/2`「使用失败」，不扣背包。  
- Pixi `GameSession.useItem`：仅 `fb.ok` 时 `handleBodies`。  
- Pixi 背包「使用」：仅成功时 `openBag`；失败只提示「使用失败」。

## 验证

- 对无效果道具使用 → 提示使用失败，数量不变。  
- 药品/经验卡成功路径仍扣减并刷新。  
- `make -j2`（服务端）；Pixi 侧按现有 smoke 自测。
