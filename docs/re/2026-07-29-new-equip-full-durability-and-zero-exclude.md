# 新获装备满耐久 + 耐久 0 不计属性（2026-07-29）

## 1. 现象

1. 刚获得的背包装备（购买 / 掉落 / 开箱 / 强化回写等）详情里当前耐久常为 `1/max`，不是满值。
2. 穿戴耐久已耗尽（当前为 0）的装备时，服务端角色属性仍把该件加成算进去。

## 2. 根因

### 2.1 背包装备 wire count

客户端对装备 id：`7/7` / 背包格子的 u32 `count` 写入 **当前耐久**（`item+0x110` 一带），不是「件数 1」。

权威侧背包行 `item->count` 对装备固定为实例数 `1`，此前多条发放/同步路径原样下发 `1`，客户端就显示 `1/max`。

穿戴位另有 `account_role_equipment_durability`；未穿戴的背包件无独立耐久表，契约是 **满耐久**。

### 2.2 属性汇总未排除耐久 0

`JianghuOL.CBE:0x0100FFEA`：`ldrsh` 读耐久，`cmp #0` / `ble` 则跳过该件加成。

`vm_net_mock_role_collect_equipment_bonus` 原先只按 `equippedItemIds` + 等级门槛累加，未读 `role_service` 耐久。

## 3. 修复

| 点 | 改动 |
| --- | --- |
| `vm_net_mock_backpack_equipment_wire_count` | 仅查询 `equip.dsh` 上限；**不要**写入背包授予 count |
| **`30/21` 登录格子 `grid_wire_count`** | **必须仍为实例 `count`（装备=1）** |
| **背包 `7/7` / 掉落 / 购买 / 强化同步 / 开箱** | **装备 count 保持 1**（曾误发满耐久导致客户端复制填包、脱装失败；见 `2026-07-29-enhance-attr-gray-and-unequip.md`） |
| 穿戴位 equipment blob | 当前耐久（DB） |
| `collect_equipment_bonus` | `service->durability[slot]==0` 跳过 |

背包装备详情「满耐久」若仍显示 `1/max`：不能用 count 承载耐久（会填包）；
穿上后以穿戴位 blob 为准。未穿戴件权威无独立耐久表。

### 回退：登录格子不可发满耐久（2026-07-29 同日）

触发：上线后背包被一件已强化装备「填满」。

`0x01039952`→`0x0101918E`：格子 `count` → `strh` 到 `item+0xf2`，并可 `*accum += count`。
把 `equip.dsh` 耐久上限（如 50）当作格子 count 后，客户端按堆叠数量膨胀占用。

契约拆分：

- `1/30/21` / 登录播种：装备 `count=1`（件数）
- `7/7` 穿戴位 blob：装备 `count`=当前耐久

穿戴位登录/同步仍走既有 equipment blob（当前耐久来自 DB）；新装备入栏 `itemChanged` 仍初始化为满耐久。

## 4. 验证

- [ ] 商店买装备 / 仓库取回装备：详情当前耐久 = `equip.dsh` 耐久。
- [ ] 开箱抽到装备：提示仍「获得1个…」，详情满耐久。
- [ ] 战斗掉装备进包：满耐久。
- [ ] 强化成功后背包该件仍满耐久且 `(+N)` 正确。
- [ ] 穿戴耐久打到 0：actorinfo/战斗攻防不再含该件加成；修理后恢复。

## 5. 相关

- `docs/re/2026-07-22-battle-equipment-durability.md`
- `docs/re/2026-07-24-equipment-repair-durability-max.md`
- `docs/re/2026-07-29-gold-chest-reward-count-tip.md`
