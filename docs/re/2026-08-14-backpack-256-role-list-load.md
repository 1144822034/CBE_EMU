# 背包扩到 256 后登录角色列表不加载

## 触发

Pixi 将角色背包扩容到 **256** 后重新登录：选服成功，但标题角色列表为空 /
不出现角色（`1/1/4 actorinfo` 侧表现为 `roleCount=0`）。

## 链路

1. 扩容写入 `role->backpackCapacity=256`，MySQL `account_roles.backpack_capacity`
   （已迁 SMALLINT 时可完整保存）。
2. 再次登录 → `vm_net_mock_role_db_load` →
   `vm_net_mock_role_db_load_mysql_relational` →
   `vm_mock_mysql_role_detail_row`。
3. 行校验仍把 `backpack_capacity` / `backpack_item_count` 上限写成 **`> 255`**。
4. `capacity=256` 整行 `invalid` → relational load 失败 →
   `g_vm_net_mock_role_db_valid=false` →
   `vm_net_mock_build_title_role_list_actorinfo` 发出 `count=0`。

## 根因

`2026-08-11-backpack-capacity-256.md` 已把内存字段与表列升到 u16/SMALLINT，
但 **MySQL 角色详情行解析器**仍沿用 TINYINT 时代上界：

- `number[4]`（capacity）`> 255` → 拒绝
- `number[15]`（item_count）`> 255` → 拒绝

权威数据存在，首次偏离在 load 校验，不是选服协议本身。

## 修改

- `vm_mock_mysql_role_detail_row`：capacity / item_count 上界改为
  `VM_NET_MOCK_BACKPACK_CAPACITY_LIMIT` / `VM_NET_MOCK_BACKPACK_MAX_ITEMS`（256）。
- 启动/存盘路径经 `vm_net_mock_role_ensure_pk_columns` 调用
  `vm_net_mock_role_ensure_backpack_u16_columns`：若列仍为 `tinyint` 则
  `MODIFY` 为 `SMALLINT UNSIGNED`（对齐 `migrate_backpack_capacity_256.sql`）。

## 验证

1. `make -j2`
2. 角色 `backpack_capacity=256` 账号重新登录：角色列表应出现该角色
3. 服务日志不应再因该账号出现 `mock_role_db_mysql_load_failed`
4. （旧库）若列仍为 tinyint，首次 load/save 应打
   `mock_backpack_u16_columns widened`
