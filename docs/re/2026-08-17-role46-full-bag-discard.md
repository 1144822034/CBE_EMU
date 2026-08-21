# 角色46背包满无法丢弃

## 触发

角色 `role_id=46`（账号 `07060707`）`backpack_capacity=256` 且
`account_role_backpack` **256 行**，多为同名白装（30026×57、30027×56、
23504×30、23304×25）。客户端点丢弃无效果或随后物品仍在。

## 根因

1. **丢弃回包缺 30/21**：`7/4` 成功后只附 `17/1`（无 seq）。Pixi
   `handleBodies` 用 `Map<itemId→row>` 回填 seq，**同名装备全部塌成同一
   seq**，后续丢弃打错/打空行。
2. **`30/21 gridnum` 仍为 u8**：满 256 时 `(u8)256=0`，与已修的 `17/1`
   i16 行数同类缺陷（`2026-08-11-backpack-capacity-256.md` 漏改 gridnum）。
3. 客户端丢弃曾把壶/凭证的 `bag.count`（储量/耐久）当丢弃数量发送。

首次偏离：满包同名白装第一次丢弃后的 17/1 刷新，不是「背包满禁止丢弃」契约。

## 修改

- 服务端：`gridnum` 改 `u16`；丢弃成功路径追加 `30/21`（+壶 `7/11`）。
- 客户端：无 seq 的 17/1 用 `mergeBag17WithGridSeq(list, priorBag)`；
  丢弃数量走 `bagVisibleStackCount`；确认框失败有 tip。
- 运维：库内删除角色 46 上述四类白装垃圾并重排 `slot_index`（需重登）。

## 验证

1. `make -j2`，部署服务端；刷新 Pixi。
2. 角色 46 重登：背包约 88/256，可连续丢弃同名白装。
3. 日志：`mock_item_discard ... refresh=7/4+17/1+30/21+...`；
   `mock_backpack_grid ... gridnum=256`（满包时非 0）。

## 后续（2026-08-19）

角色 46 再次满 256 后操作又空转：局部 `u8 itemCount` 承接
`backpack_count` 时 256→0。见 `2026-08-19-role46-full-bag-u8-count.md`。
