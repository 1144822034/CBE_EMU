# 2026-09-29：装备重铸改扣一级玄晶 + 洗练转移开关

phase: equip-recast-crystal-fee  
status: implemented

## 变更

### 重铸消耗

- 原：29/6 固定扣 48金65银（486500 铜）。
- 现：源强化 **+1 免费**；**+N(N≥2)** 扣 **1×(N-1)级玄晶**（例：+5 → 四级玄晶 904）。
- 后台开关：`transfer_crystal_enabled`（默认开）、`transfer_money_enabled`（默认关，开则另扣 48金65银）。
- 不足时 `result=3`，Pixi tip「玄晶不足」。
- 29/5 `money` wire 改为 0（不再驱动金币费用显示）。
- 跨品重铸石(855)逻辑不变。

### 洗练转移配置

- 管理后台「强化配置 → 洗练 / 重铸规则」：
  - **重铸是否转移洗练属性**（默认开）
  - **跨品质重铸是否需要跨品重铸石**（默认需要）
- 表字段：`transfer_wash_enabled`、`cross_quality_stone_required`
- 跨品需石=关：同部位即可跨品质，29/5|6 不检查/不消耗 855；人物面板下发 `crossQualityStoneRequired` 供 Pixi 列表筛选。
- 开：源洗练随强化拷到目标并清源（原行为）。
- 关：源洗练丢弃，目标保留原洗练。
- 29/6 成功响应附加 Pixi 字段 `washxfer`（0/1）供客户端本地镜像。

## 关键文件

- `src/server/mock_server_equipment_npc.c` — 29/5|6 扣费与洗练分支
- `src/server/mock_server_enhance_config.inc.c` — 设置读写
- `src/web_admin_enhance.inc.c` — 后台表单
- `PixiJS_web/src/ui/EnhancePanel.ts` / `protocolExtras.ts` / `GameSession.ts`

## 验证

1. +1 装备重铸：不扣玄晶、不扣金。
2. +N 装备重铸：扣 1 个 (N-1) 级玄晶；无玄晶 tip「玄晶不足」。
3. 后台关「转移洗练」：重铸后源洗练空、目标洗练不变；开则源→目标。
4. `make -j2` 通过。
