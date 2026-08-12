# Pixi 背包上限 256

## 背景

CBE `InitTimerControl` 曾把客户端格子硬封在 64；项目已以 Pixi 为准，
CBE 仅作美术资源参考。

## 根因 / 契约

1. `VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY` 等曾为 64，扩容与
   `17/1 maxnum` 被夹断。
2. `role->backpackCapacity` / `backpackItemCount` 为 `u8`，MySQL
   `TINYINT UNSIGNED` 无法完整表达 256。
3. `17/1 iteminfo` 行数原为 tagged `u8`，满 256 行会截成 0。

## 修改

- 上限常量：**256**（logical / capacity limit / max items）
- 内存：`backpackCapacity`、`backpackItemCount` → `u16`
- MySQL：`SMALLINT UNSIGNED` + `migrate_backpack_capacity_256.sql`
- `17/1` 行数：tagged **i16**；Pixi `parseBagItems17` 同步改读 `i16`
- actorinfo 仍写历史 `u8` 容量槽（>255 夹到 255）；Pixi 以 `maxnum` /
  角色属性 `backpack` 为准

## 验证

1. 执行 `server/mysql/migrate_backpack_capacity_256.sql`
2. `make -j2`
3. 将 `server_code_version.txt` 与客户端 `client_code_version.txt` 升到 **7**
   （`17/1` 行数改为 i16，旧客户端会解错背包）
4. 登录后开背包：容量可扩到 256；`17/1`/`30/21` 列表完整
5. 使用扩容道具 806，逐步升到 256 后不可再扩
6. 容量已是 256 时商城再买 806：提示「背包已达上限，不能再扩容」（`result=3`）
7. 806 详情说明追加「最大可扩容到256」（`itemDisplayDesc`）
