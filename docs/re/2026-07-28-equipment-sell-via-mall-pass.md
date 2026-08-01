# 装备出售：商城耐久凭证 + 26/1 对话框（全额铜币）

## 需求

与仓库凭证用法一致：商城购买耐久凭证 → 使用 → 弹出对话框选择背包装备。
区别：售出后**不可找回**，按装备目录 `价值` **全额**兑换铜币（不是丢弃的 `floor(价值/10)`）。

## 契约

| 项 | 值 |
| --- | --- |
| 商城道具 | `839` 装备出售凭证（`item.dsh`；酷宝价同 834；**类别 10**） |
| 耐久 | 背包行 `item_count`，购入 `50`；每次使用扣 1 |
| 打开 | 主 CBMR：`7/1+7/7+7/11`；同连接第二 CBMR：lone `26/1`（`HAS_FOLLOWUP`） |
| 会话 | 每客户端 `equipSellSessionArmed` / `equipSellDialogPending`（禁止进程全局） |
| 列表 | `0xF1010000\|page`：仅装备；点选先 `0xF3000000\|seq` 查看属性 |
| 详情 | 主文复用商店装备详情（等级/售价/属性/耐久；有强化则附带）；确认 `出售该装备` |
| 出售 | `0xF2000000\|backpack_seq`：删行 + `money += catalog->price`（全额） |
| 铜币刷新 | 出售成功的 `26/1` 后追加 `1/10/26`（与丢弃补偿同源） |
| 背包 UI | 同仓库存入：poll peel `17/1+7/42` 再 `26/0` |

对照：`2026-07-25-equipment-discard-refund.md`（十分之一）、`2026-07-27-warehouse-dialog-via-mall-pass.md`（对话框路径）。

## 2026-07-29：835 背包看不见（已修）

**根因：** `item.dsh` 原有 `835 红玫瑰`。首版把出售凭证也写成 `835` 并追加一行，形成重复 ID。
客户端按 ID 查本地 DSH 时命中「红玫瑰」行（类别/形象与凭证不符），服务端 `find_shop_catalog_item(835)` 也返回第一行红玫瑰；购入/展示契约错位，背包表现为看不见或显示异常。

**修复：** 删除重复的出售凭证 `835` 行；改用空闲 ID **`839`**。常量 `VM_NET_MOCK_EQUIP_SELL_PASS_ITEM_ID=839`。

若背包里已有错误购入的 `835` 行，那是红玫瑰 ID，需丢弃后重新购买 `839`。

## 修改点

- `item.dsh`（`bin/`、`web/fs/`、Android assets）：839；去掉错误的重复 835
- `mock_server_core.c`：opcode / 常量
- `mock_server_catalog.c`：839 使用 → arm + `7/1+7/7+7/11`
- `mock_server_equipment_npc.c`：会话字段 + arm/offline clear
- `mock_server_role.c`：购入耐久行；出售 helper；不可入库
- `mock_server_scene_sync.c`：wire/poll `26/1` + 出售菜单
- `mock_server_transport.c` / `mock_server_social.c`：第二 CBMR / poll 兜底

## 验证

1. `make server`；部署服务端；确保 `resource_root` 的 `item.dsh` 含 **839** 且 **835 仅红玫瑰**
2. Android/PC 客户端同步更新本地 `item.dsh`
3. 商城买 839 → 背包可见「装备出售凭证」→ 使用弹出售入口
4. 日志含 `mock_equip_sell_pass_use`、`mock_equip_sell_dialog_wire`
5. 出售一件已知价值装备：铜币 +全额 `价值`，背包行消失且不可取回
