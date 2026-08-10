# 梦境副本运营规则（2026-08-03）

## 已实现

| 项 | 行为 |
|----|------|
| 1 分钟提醒 | TTL 剩余 ≤60s 时系统消息一次：`1分钟后强制离开梦境副本！` |
| 周围玩家 | `29*` 场景不 append nearby role / moveinfo |
| 禁组队 | 梦境内每 10s 检测；组队则提示 `梦境副本禁止组队`、拆队并出口；创建/加人亦拦截 |
| 地图显示名 | 场景设计页可改：MySQL `server_scene_display_names` + 回写 SCE 具名传送 `0x16` 名牌并发布 |
| 怪物显示名 | 怪物管理保存可改 `display_name`（目录表） |
| 每日进入 | 场景设计页配置 `daily_limit` / `gold_cost`(金) / `kubao_cost`；`build_instance_enter_response` 发 30/1 前调用 `instance_enter_try_consume` 扣次+扣费，不足拒绝（对话 + 系统消息）；组包失败回滚 |
| 商城 | 梦境内**允许**打开商城（原禁商城已取消） |
| 禁仓库 | 梦境内不可用仓库：834 凭证使用拒绝（**`7/1 result=0` + `16/2` 提示**，避免只回 tip 卡 loading）；26/1 存取/菜单拒绝；pending 对话框丢弃；提示 `梦境副本禁止使用仓库。` |

## 2026-08-05 梦境用 834 卡住

### 根因

拦截路径只回 `16/2` hint。834 走通用 `7/1` 等待
（`HandleItemOperationResponse`），无 `7/1` 对象则进度条不消。

### 修复

`mock_warehouse_pass_use_blocked_dream` → `7/1{result=0}+16/2{hint}`；并按
`role->scene` 兜底判定梦境。

| 离本清物 | 离开 `29*`（踢出 TTL / 禁组队驱离 / 使者传出 / 传送石 / 死亡回城 / 登录强制迁出）时，删除背包全部物品 `841`，并 poll 推送 `17/1` 背包列表同步客户端 |

## 配置

- 墙钟 TTL：`JHOnlineData/dream_hud_ttl_ms.txt`（默认代码 3600000）
- SQL：`server/mysql/migrate_add_instance_enter_rules.sql`（启动时 `CREATE IF NOT EXISTS`）

## 2026-08-08 进本扣费接线

`vm_net_mock_instance_enter_try_consume` 此前仅定义、未调用，后台配置不生效。
已接到 `vm_net_mock_build_instance_enter_response`（使者进本 / smoke / 同路径 30/1）。
目标场景无规则或三项皆 0 → 免费；出本到无规则户外不扣费。

## 已知边界

- 梦境图客户端可见名主要来自 SCE 传送点字段 `0x16`（不是 `sMap.dsh`；梦境行本就不在 sMap）。
- 改名后需让客户端拉到新 SCE 版本（重进图 / 资源更新）；纯刷新后台页不够。
- 怪物名写入目录后，开战名立即生效；地图刷怪点名牌需在场景设计里重新保存刷怪点。
- 客户端酷宝 UI 刷新依赖后续 `1/14/4` / 角色脏数据同步；扣费以服务端 `role->wcoin` 与日志 `instance_enter_consume` 为准。
