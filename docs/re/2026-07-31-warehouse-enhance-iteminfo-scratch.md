# 仓库取回/存入：强化词条 iteminfo scratch 过小

日期：2026-07-31

## 触发与现象

仓库取回强化 ≥4（带 `ParseEquipAttributes` 词条）的装备时，客户端背包
可能不出现该行；或存入后 `17/1` 列表刷新失败，客户端仍残留/缺行。
库表 / 内存仓库与背包计数往往已正确。

## 业务链路

1. 取回：`26/1 action=warehouse-retrieve` → 内存入包 → arm
   `npcPurchaseBackpackPending` → poll 投递 `7/7 type=1`
   （`append_backpack_item_add7_object`）。
2. 存入装备：`26/1 deposit` → arm `backpackListResync` → poll 推
   `17/1`（+ `7/42`）权威列表。
3. 两路都经 `seq_put_item_common_extra`；L≥4 时 common-extra 可达约 89
   字节（`CLIENT_CAP=6`）。

## 根因

| 路径 | 旧 scratch | 单行最坏 | 后果 |
| --- | --- | --- | --- |
| 取回 `7/7 type=1` | `itemInfo[64]` | ≈108 | 编码失败 → pending 返回 0，客户端无入包 |
| 存入后 `17/1` | `BACKPACK * 27` | ≈95/行 | 满包强化装可能溢出，list-resync 失败 |
| 登录装备播种 | `itemInfo[512]` | 8×≈105 | 见同日 login-equip 文档 |

首次偏离：服务端编码缓冲区按「零词条」尺寸估算，强化词条落地后写满失败；
不是仓库表丢行。

## 修改

- `ITEMINFO_ROW_WIRE_MAX=128`，`ITEM_USE_ITEMINFO_SCRATCH=8+128`。
- `append_backpack_item_add7_object`（仓库取回 / NPC 购买入包）改用该
  scratch；失败打 `mock_backpack_add_encode_failed`。
- `BACKPACK_ITEMINFO_SCRATCH` 改为 `8+MAX*128`（覆盖 30/21 与 17/1）。
- 强化 sync、交易收货、战斗掉落、附近装备查看等同口径扩大。

## 验证

1. `make -j2`，重启服务。
2. 将强化 ≥4 装备存入仓库再取回：应见
   `mock_backpack_add ... enhance=N iteminfo_len>64`，背包显示 `(+N)`
   与强化附加。
3. 满包装强化装备登录 / 存入后 list-resync：不应再出现
   `mock_backpack_grid_encode_failed` / 静默 `response=0`。
4. 未强化装备取回仍无假 `(+1)`（见
   `2026-07-27-warehouse-retrieve-enhance-plus-one.md`）。
