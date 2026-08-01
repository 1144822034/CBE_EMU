# 登录背包空：7/7 type=2 装备播种 scratch 溢出拖垮 group-type1

日期：2026-07-31

## 触发与现象

账号 `wwe666wwe` / 角色 `5`：MySQL / 内存背包有物品
（`stored_rows=53`，`gridnum=52`），但进游戏背包不显示。

日志：

```text
mock_backpack_grid_reseed role=5 reason=title-role-select next=group-type1-30/21
mock_backpack_grid role=5 kind=30 subtype=21 gridnum=52 stored_rows=53 iteminfo_len=1404
mock_backpack_reservoir_seed role=5 rows=14 info_len=143 response=7/11
mock_group_type1_backpack_seed_failed role=5 evidence=30/21-or-7/7-type2-encode abort-empty-response
unhandled wt=5/10 len=35 ... response=0 source=ignored-unhandled-server-only
...
mock_backpack_items role=5 capacity=64 rows=52 stored_rows=53 iteminfo_len=887
```

选角已武装 `30/21`，网格与 `7/11` 水库都已在编码路径打出成功日志，但随后
`5/10 + 7/7(type=1)` 整包返回空；场景 followup 的 `17/1` 仍能列出 52 行，
说明权威背包在服务端，缺的是登录网格播种到达客户端。

## 根因

`append_backpack_role_grid_main_objects` 在一次 group-type1 响应里顺序追加：

1. `30/21` 背包网格
2. `7/11` 水库计数（若有）
3. `7/7 type=2` 身上装备登录播种

`append_equipment_login_object` 使用固定 `u8 itemInfo[512]`。  
角色 `durability_slots=8`（满装），且强化 common-extra 可含最多
`CLIENT_CAP=6` 词条时，单行约：

| 字段 | 带 tag 字节 |
| --- | --- |
| seq(i16)+itemId(u32)+durability(u32) | 16 |
| common_extra（2×i16 + u8 + 6×attr） | ≤89 |
| **每槽合计** | **≈105** |
| 8 槽 + rowCount | **≈843 > 512** |

`build_equipment_login_iteminfo_blob` 写满失败 →
`append_backpack_role_grid_main_objects` 失败 →
`build_group_type1_response` 返回 **0**。  
此前已写入缓冲区的 `30/21` 随整包丢弃，客户端从未走
`HandleItemGridResponse(0x01039952)`，主物品管理器为空。

首次偏离：服务端登录 `7/7 type=2` 编码缓冲区过小；不是库表丢数据，也不是
`30/21` 本身溢出（该项已成功编出 1404 字节）。

与 `2026-07-26-login-backpack-30-21-iteminfo-overflow.md` 同类：group-type1
任一步编码失败都会把已编好的背包网格一起丢掉。

## 修改

- `VM_NET_MOCK_EQUIP_LOGIN_ITEMINFO_SCRATCH = 8 + SLOT_COUNT*128`（≥8×105）。
- `append_equipment_login_object` 改用该 scratch；编码失败打
  `mock_equipment_login_encode_failed` / `mock_equipment_login_put_failed`。
- group-type1 各步失败打 `mock_group_type1_backpack_step_failed step=...`，
  避免再只剩模糊的 `30/21-or-7/7-type2`。

## 验证

1. `make -j2`，重启 mock-service。
2. 同账号重登：应见 `builtin-group-type1`、`mock_backpack_grid`、
   `mock_equipment_login rows=8`（或实际穿戴数），**不应**再出现
   `unhandled wt=5/10` / `mock_group_type1_backpack_seed_failed`。
3. 客户端背包应显示库内物品；身上装备 UI 也应有对应件数。
