# NPC 装备购买列表增加「查看」属性

日期：2026-07-26

## 变更

武器 / 防具商人商品页对齐商城交互：列表一行一件（名称+售价，说明为
`查看属性`），点选后进入详情页展示 `equip.dsh` 属性，再「购买该装备」或
「返回商品列表」。每页仍 5 件（加翻页），不再每件占两个选项。

商城 `mmShop` 的「查看」是客户端本地读 `equip.dsh`、无额外 WT；NPC 对话
不能进 mmShop UI，因此用 `VIEW_ITEM` 详情页复现「先看再买」。

## 契约

- opcode：`VIEW_ITEM_BASE = 0xed000000 | itemId`（`action=1` → `26/1 type=2`）。
- 装备列表值指向 `VIEW_ITEM`；购买只在详情页的 `BUY_ITEM`。
- 会话记录 selector / levelBand / page，供返回列表。
- 药品商人仍为列表直接购买。

## 修改点

- `src/server/mock_server_core.c`：`VIEW_ITEM_BASE`、页容量 5
- `src/server/mock_server_equipment_npc.c`：`npcShopBrowse*` 会话字段
- `src/server/mock_server_scene_sync.c`：列表选中→详情→购买/返回
