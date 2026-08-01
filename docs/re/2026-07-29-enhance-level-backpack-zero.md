# 强化成功后背包强化等级仍为 0

## 触发与现象

装备强化成功（`29/3 result=1`）后进入主背包，该件仍显示 `(+0)`，
尽管服务端 `backpackItems[].enhanceLevel` / MySQL 已递增。

## 业务链路

1. 开强化 `29/1`：`0x01028C7C` 把 `curlevel`→session`+8`，`maxlevel`→`+0xc`，
   `curlevel+1`→`+0xa`；并以 mode=0 调 `0x010287C0` 把**当前**等级写到
   强化 UI 列表项 `item+0xe`。
2. 确认 `29/3` 成功：同函数以 mode=1 调 `0x010287C0`，把 session`+0xa`
   （目标等级）写到**该列表**匹配 `equipseq` 的项。
3. 响应契约旧实现仅 `result/tnum/equipseq/occult`，无背包行权威同步。
4. 主背包名称绘制读背包行 `item+0xe`（common-extra **第一** `i16`），不是强化 UI 列表。

## 首次偏离

`29/3` 成功处理后：强化 UI 列表可能已是新等级，但主背包行 common-extra /
`item+0xe` 仍为强化前值。打开背包读主背包行 → `(+0)`。

另：旧 common-extra 把等级写在**第二** i16，登录 `30/21` 也会使 `+0xe=0`；
见 `2026-07-29-login-backpack-enhance-zero.md`。

## 根因

- `29/3` 不返回新强化等级；客户端本地 bump 只打在强化 UI 列表。
- 主背包行需要与物品使用相同的 `7/7 type=2`（iteminfo 含 enhance）+
  `7/11`（count=1）才能重写 `ParseEquipAttributes`。
- 此前 inventory sync 只同步玄晶剩余与铜币，未同步被强化装备。

## 修改

`vm_net_mock_append_equipment_enhance_inventory_sync`（`29/3` result 1/2）：

1. 仍：每个扣除玄晶 `7/7 type=2`+`7/11` + `1/10/26` 铜币。
2. **成功时额外**：被强化装备 seq 追加 `7/7 type=2`（count=1，common-extra
   enhance=新等级，含 L≥4 词条）+ `7/11` count=1。
3. 编码失败则回退仅 `29/3`（best-effort）。

## 验证

- [ ] 强化 0→1 成功后直接开背包：显示 `(+1)`；日志含 `equip` sync。
- [ ] 强化失败（result=2）：装备等级不变；仍同步玄晶/铜币。
- [ ] 重登后等级与 MySQL `enhance_level` 一致。
- [ ] `make -j2` 通过。

## 状态

- 主背包强化显示：已修（装备 `7/7`+`7/11`）。
- 与 `2026-07-29-enhance-crystal-bag-count-stale.md` 共用 inventory sync 入口。
