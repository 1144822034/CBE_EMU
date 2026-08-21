# 角色46满包256：背包操作无反馈

日期：2026-08-19  
环境：`192.1.1.3` / `jh_online` / role_id=46（账号 `07060707`）

## 触发

比武窗口后玩家仍在 `c04比武擂台`。打开背包：使用 / 装备 / 丢弃均无实质效果
（丢弃后物品仍在，或感觉「没有任何反馈」）。

## 库侧证据

| 字段 | 值 |
|------|-----|
| `backpack_capacity` | 256 |
| `backpack_item_count` / 行数 | **256**（slot 0–255） |
| 场景 | `c04比武擂台` |
| 白装堆积 | 30026×25、23304×24、30027×23、23504×21（与 8/17 同类） |

当日比武配置已改到上午：`entry 590–599`、`match 600–620`；session=7 已 SETTLED。

## 根因

`2026-08-11-backpack-capacity-256.md` 已把 `role->backpackItemCount` 扩成 `u16`，
且 `17/1`/`30/21` 行数改为可表达 256。但若干热路径仍用 **局部 `u8 itemCount`**
承接 `vm_net_mock_role_backpack_count()`：

```c
u8 itemCount = 0;
itemCount = vm_net_mock_role_backpack_count(role); /* 256 → (u8)0 */
for (u32 i = 0; i < itemCount; ++i) ...
```

满 256 时循环 0 次。首个偏离在：

`vm_net_mock_role_find_backpack_item` — 按 seq/itemId 查找恒失败。

后续契约破坏：

1. **丢弃 7/4**：找不到行 → `mock_item_discard_stale_resync` 仍回 `result=1`（清客户端等待），
   **不扣物品**；Pixi 仍提示「已丢弃」并 `openBag`，列表仍是 256 → 像「没反馈」。
2. **使用 / 装备**：同样找不到行 → 操作空转或失败。
3. 同文件其它 `u8 itemCount`（grant / consume_amount / flask / purge 841 等）在满包时
   也会把 256 当成 0（空间判断、遍历错误）。

这与 8/17「同名白装 seq 塌缩」不同：本次是 **满 256 的计数宽度**；
清理到 <256 后旧二进制会暂时恢复，再堆满又复发（角色 46 已再次满包）。

## 修改

局部 `u8 itemCount`（承接 `backpack_count`）改为 `u32`：

- `mock_server_role.c`：`find_backpack_item`、by_effect、consume_amount、grant/flask、purge 841 等
- `mock_server_equipment_npc.c`：swap / trade_role_add_item
- `mock_server_scene_sync.c`：item_total / can_receive

交易报价里的 `trade_offer.itemCount`（槽位数）保持 `u8`，与背包无关。

## 验证

1. `make -j2`
2. 部署 `192.1.1.3` 服务端；角色 46 **重登**（内存 role 才走新代码）
3. 满 256 时丢弃一件白装：库行数减 1，物品从列表消失；日志应见
   `mock_item_discard ... result=1` 且 **没有** `stale_resync`（除非真幽灵 seq）
4. 使用/装备非满包路径回归即可

## 运维（可选，不替代代码修复）

部署前若需立刻腾格：删部分 30026/30027/23304/23504 白装并重排 `slot_index`，
**必须重登**；否则进程内仍是满 256。根因修完后不应再依赖清库兜底。
