# 装备强化等级穿戴后丢失（2026-07-27）

## 触发与首次偏离

强化成功（`29/3 result=1`）后背包装备带有 `enhance_level`；穿上再卸下后，
背包里该件强化等级变为 0。

## 根因

权威状态里：

- 背包行：`account_role_backpack.enhance_level` / `backpackItems[].enhanceLevel`
- 穿戴槽：仅有 `equippedItemIds[slot]`，**没有**强化等级

因此：

1. 穿戴时消耗背包装备，只写入 `itemId`，强化等级被丢掉；
2. 卸载时 `add_backpack_item` 新建行并硬编码 `enhanceLevel=0`；
3. `7/9` 替换路径同样把卸下的旧装写成 `enhanceLevel=0`。

客户端本地在穿戴期间仍可能显示强化，但权威存档与卸载回包已是 0，重登或
再操作后表现为「强化丢失」。

## 修复

- 角色态增加 `equippedEnhanceLevels[slot]`，与 `equippedItemIds` 同步读写。
- MySQL `account_role_equipment.enhance_level`（启动时自动 `ALTER`；亦提供
  `server/mysql/migrate_add_equipped_enhance_level.sql`）。
- 穿戴 / 卸载 / `7/9` 替换双向搬运强化等级；回包登录 `7/7 type=2` 与附近
  `equipinfo` 的 common-extra 第二 `i16` 带上该等级。

## 验证

- [ ] `make -j2`
- [ ] 强化一件装备到 ≥1 → 穿上 → 卸下：背包行强化仍为原等级；MySQL
      `account_role_backpack.enhance_level` 与穿戴期 `account_role_equipment.enhance_level`
      一致
- [ ] 重登后身上装备与卸下后背包均保留强化
- [ ] 未强化装备穿戴卸载仍为 0
