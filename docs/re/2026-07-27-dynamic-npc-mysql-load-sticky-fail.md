# server_dynamic_npcs 有数据但铜雀台 NPC 全不显示

日期：2026-07-27

## 现象

`server_dynamic_npcs` 已配置铜雀台 3 行（郭芙蓉 / 白展堂 / 大侠郭靖），客户端进图后
一个都不显示。日志常见：

```text
dynamic_npc_db_load failed error=MySQL socket receive failed
mock_scene_npc_collect_empty ... db_valid=0
mock_scene_npc_exact_validate ... rows=0
mock_scene_npc_catalog ... actors=0 selected=0 dynamic=0
```

## 根因

1. **启动加载 MySQL 失败被 sticky 住**：`dynamic_npc_db_load` 原先无论成败都置
   `loaded=1`；socket 超时后 `valid=0`，之后整进程再也不重读表，内存目录空。
2. **启动顺序**：XSE/NPC 校验会 collect 动态 NPC；若紧挨着 `shop_item_db_load`
   超时关连接，动态 NPC 查询更容易失败。
3. （次要）地图石 `wait-wt6` 曾被 `type27-followup` 抢先消费空 `27/11`（另见
   `2026-07-27-teleport-type27-steals-npc-seed.md`）。

表数据本身正确；问题在「启动时没进内存 + 失败不重试」。

## 修改

1. 仅在加载成功时置 `loaded/valid`；失败清空 overrides，关连接，最多重试 3 次，
   仍失败则下次 collect 再试。
2. `battle_catalog_warmup` 提前到 XSE 校验之前，并包含 `dynamic_npc_db_load`。
3. 保留 type27 对 `wait-wt6` 的 hold（不消费一次性目录）。

## 验证

重启后启动日志应类似：

```text
battle_catalog_warmup ... dynamic_npc=ok
dynamic_npc_db_load rows=22 ... (或至少含铜雀台 3 行)
mock_scene_npc_exact_validate scene=c00蓬莱仙岛_01 rows=3 ...
```

瞬移进铜雀台：`seed_deliver ... npcnum=3`（或 selected≤4），画面可见 3 个 NPC。
