# 仓库存取金币（以金为单位，防刷）

## 需求

在已有 834 凭证 + `26/1` 仓库对话框上增加「存入金币 / 取出金币」；
UI 以**金**为单位（1金=10000铜），服务端权威转账，避免刷金。

## 入口

| 项 | 值 |
| --- | --- |
| 打开 | 使用 834 → 第二帧 `26/1` 仓库根菜单（与物品存取同一会话） |
| 根选项 | 取回物品 / 存入物品 / **存入金币** / **取出金币** |
| 金额档 | 1 / 10 / 100 / 1000 金（按余额过滤）+ 全部存入/取出 |
| 存储 | `account_role_warehouse_bank.money`（铜，整金对齐） |

## Opcode（复用 26/1 action=1，不伪造钱庄 WT）

| 常量 | 值 |
| --- | --- |
| `MONEY_DEPOSIT_MENU` | `0xee030000` |
| `MONEY_WITHDRAW_MENU` | `0xee040000` |
| `MONEY_DEPOSIT_BASE \| gold` | `0xee050000`；`gold=0xffff` 为全部 |
| `MONEY_WITHDRAW_BASE \| gold` | `0xee060000`；`gold=0xffff` 为全部 |

## 防刷契约

1. 必须 `warehouseSessionArmed`（先用 834）；梦境副本仍禁仓。
2. 只移动整金铜量：`copper = gold * 10000`；「全部」也先 floor 到整金。
3. **先扣源、再加目标**；两侧做 u32 溢出检查；余额不足拒绝。
4. 内存转账后 `mark_inventory_dirty`，与角色 `money` + 仓库 bank 同快照 flush（bank 表不随物品 DELETE 清空）。
5. 成功路径追加 `1/10/26 {money=role->money}`，避免客户端用旧余额连点。
6. 客户端不可自报金额：仅选服务端下发的档位 / 全部。

## 修改点

- `mock_server_core.c`：opcode、`warehouse_state.money`
- `mock_server_role.c`：bank schema/load/write、`warehouse_money_transfer`
- `mock_server_scene_sync.c`：根菜单 + 金额菜单 + refresh
- `server/mysql/schema.sql`、`migrate_add_warehouse_bank.sql`

## 验证

1. `make -j2`；部署服务端；必要时跑 migrate。
2. 使用 834：根菜单可见存/取金币，文案含仓/身金数。
3. 存入 1 金：身上 −1金、仓 +1金；重登后仍在。
4. 余额不足档位不出现；连点全部取出不会超支。
5. 日志含 `mock_warehouse_money_deposit|withdraw`。

## 风险

- 未跑 migrate 时首次开仓会 `CREATE TABLE IF NOT EXISTS`（与物品仓同路径）。
- 非整金铜币留在身上，不进仓（刻意）。
