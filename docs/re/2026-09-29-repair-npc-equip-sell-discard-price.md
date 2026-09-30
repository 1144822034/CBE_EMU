# 修理 NPC：装备出售（复用出售凭证菜单，按丢弃价）

## 需求

装备修理服务 NPC（`npc_kind=2` / `serviceMask` bit2，如欧冶子）根对话增加「装备出售」。
菜单与结算复用装备出售凭证（839）同一套 `26/1` opcode；铜币按丢弃补偿
`floor(equip.dsh 价值 / 10)`，不再发全额 `价值`。

## 契约

| 项 | 值 |
| --- | --- |
| NPC 根选项 | `action=1` → `OPEN_EQUIP_SELL`=`0xf1000001`（与 839 根入口相同） |
| 会话 | 选中 `OPEN_EQUIP_SELL` 时 `equipSellSessionArmed=true`（NPC 不消耗 839） |
| 列表/详情/出售 | `0xF101` / `0xF300` / `0xF200` 原路径不变 |
| 单价显示与入账 | `vm_net_mock_equip_discard_refund_unit_price` = `price/10`（`<10` → 0） |
| 凭证 839 | 仍可使用；打开后同一菜单，价格同样改为丢弃价 |
| Pixi 批量 | 「出售全部 / 按品质」仅注入出售内层，不注入修理 NPC 外层 |

对照：`2026-07-28-equipment-sell-via-mall-pass.md`（原全额）、`2026-07-25-equipment-discard-refund.md`。

## 修改点

- `mock_server_scene_sync.c`：修理 kind 追加「装备出售」；`OPEN_EQUIP_SELL` 武装会话；列表/详情展示丢弃价
- `mock_server_role.c`：`equip_sell_backpack_seq` 入账改为丢弃价 helper

## 验证

1. `make -j2`
2. 对话欧冶子：可见「修理全部装备」与「装备出售」
3. 点「装备出售」→ 出售菜单（无需 839）→ 详情铜价 = 价值/10 → 确认后铜币增加同额、背包行消失
4. 使用 839 打开同一菜单，价格与 NPC 路径一致
