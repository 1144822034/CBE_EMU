# 2026-08-04 商城按钮 → NPC 购买界面（酷宝）

## 状态：已回退

酷宝 redirect（吞菜单 / refuse 拆图 / empty catalog / peel-root+30/1）导致
闪退或卡在菜单。**已还原正常 W 币 mmShop**：

- 主机：不再拦截菜单 OK / 触屏 / outdoor remove / truncate 上行
- 服务端：`shop_open14` / scene-interaction 恢复 inline 目录页（git HEAD）

酷宝 NPC `26/1` 方案待另开安全 keep-map（须 mmGame 仍在，对齐脱离卡死
`12/3+16/3`，禁止根屏 poll `30/1`）。

## 原需求（归档）

点商城按钮：**不要**进 mmShop（W币界面）；直接用现有 NPC `26/1` 购买对话框
（武器 / 防具 / 药品），扣 **酷宝**（`wcoin`），文案显示「酷宝」而非「铜」或「W」。

## 验证（回退后）

1. `make -j2`；重启 server + client
2. 菜单 → 江湖商城：应进入 **W 商城**（有商品列表）
3. 日志应有 `mock_shop_open14 ... pages=inline`，无 `coolmall_redirect` / `refuse outdoor`
