# 2026-08-07：Pixi 人物信息走服务端 build_player_stats 汇总

phase: pixi-panel-stats  
status: implemented

## 契约

PixiJS_web 与 CBE 属性页路径分开：

| 客户端 | 权威数据 |
|--------|----------|
| CBE | actorinfo 裸半字 + 客户端 wear-apply（物攻 FA / 护甲 F8 / 闪躲暴击等） |
| Pixi | **`1/1/21`** → `vm_net_mock_role_build_player_stats` **基础+装备全额** |

请求：`WT 1/1/21`（单对象，无 payload；CBE 不发）。  
响应：同对象命名字段（`strength/attack/defense/dodge/crit/...` + `base*` + `eq*`）。

## 显示

Pixi UI 复刻 CBE「属性」壳（顶栏/黄名/标签序/▲▼），数值为服务端全额合计（不再本地拼装）。  
`1/1/21` 另含 `spouse` / `guild` / `expNext`。

## 修改点

- `src/server/mock_server_role.c`：detector + builder（含配偶/帮派/下级经验）
- `src/server/mock_server_dispatch.c`：hook
- `PixiJS_web`：`buildPanelStatsRequest` / `parsePanelStats` / `openCharacterStats`；`SystemPanel` 属性页布局

## 验证

1. `make -j2` 重启 mock
2. Pixi 打开人物信息：日志 `mock_pixi_panel_stats ... evidence=build_player_stats+1/1/21`
3. 界面顶栏为「属性」、黄名、可上下翻到护甲/躲闪…活力
4. 穿戴/卸下装备后重开面板，物攻/护甲/三围与战斗权威同向变化
5. CBE 登录属性页行为不变（仍不走 1/1/21）
