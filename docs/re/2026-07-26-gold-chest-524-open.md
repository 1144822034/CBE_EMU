# 黄金宝箱 524 开启（1/7/15）

## 触发与首次偏离

背包中使用黄金宝箱时，客户端不会走通用 `1/7/1`，而是发送专用
`1/7/15`。此前服务端没有该 handler，请求落入未处理或被 category-10 的
通用消耗路径误伤。

## 卡住根因（2026-07-26 follow-up）

现象：一点开启就卡住（无失败提示）。

客户端 `0x010237e0` / `0x010237ee` 用 Thumb ADR 引用字面量池 `box\0key`。
ADR 立即数必须是 4 的倍数，两个 PC 相对寻址都落在中间的 `x\0`，因此线上
请求的两个 i16 字段名都是 **`x`**（顺序仍是宝箱 seq → 钥匙 seq），而不是
`box` + `key`。

旧解析要求存在 `key` 字段；对真实包 `parse` 失败后 `build` 返回 **0** →
客户端一直等 `7/15` 响应 → UI 卡住。

修复：按线序收集名为 `box`/`key`/`x` 的两个序号；已识别的 `7/15` 在字段
失败时回 `result=6`，禁止再回空包。

## 客户端契约

证据：`江湖OL.CBE`

| 地址 | 作用 |
| --- | --- |
| `0x01023706` | 组包 `1/7/15`；字段名因 ADR 对齐常为双 `x`（语义 box/key，i16 seq） |
| `0x010261EC` | 响应分派：`kind==7 && subtype==15`，再读 `result` |
| `0x01023850` | `result==1` 时客户端自行扣除当前开启对（宝箱/钥匙） |
| `0x010266BA` 一带 | 成功提示「获得%d个%s」；失败文案含宝箱/钥匙不存在、背包不足 |

`result` 语义：

| result | 含义 |
| --- | --- |
| 1 | 成功：`total` + `iteminfo`（奖励流，见下） |
| 2 | 宝箱不存在 |
| 3 | 钥匙不存在 |
| 4 | 背包空间不足 |
| 5 | 金币奖励（本实现未用） |
| 6 | 开启失败 |

成功路径不得再附带通用 `7/7`/`7/11` 删除包，否则会与客户端自删重复。

## 成功路径奖励流（2026-07-26 crash follow-up）

触发：`result=1` 响应已送达，客户端自扣箱/钥后解析奖励 blob，在
`pc≈0x01033A68` 未映射读（地址 4）软失败，UI 卡住。

### 根因 1（已修）：blob 行布局

曾复用 `7/7` 的 `iteminfo`（`tagged-u8 rowCount` + `seq` 优先）。成功支路用
`total` 做循环次数，每行按

`tagged-u32 itemId` → `tagged-i16 seq` → `tagged-u32 count` → common-extra

读取。前导 `rowCount` 会使首个 `i32` 错位。

正确 blob（`total` 条，无行数前缀）：

```text
tagged-u32 itemId | tagged-i16 seq | tagged-u32 count |
tagged-i16 stackRuntime | tagged-i16 enhance | tagged-u8 attrCount=0
```

`count` 必须是**本次发放增量**（当前固定为 1），不是堆叠合并后的背包绝对数量。
客户端 `0x01019228` 对 tip 做 `*out += count`，并按该值叠加本地背包；若写入
合并后总量，会出现「获得 N 个」实为「原有+1」，本地堆叠短暂虚高，重登后恢复
服务端真实数量。

### 根因 2（本次卡住）：字段名必须是 `iteminfo`

布局修好后仍软失败。客户端 dump 显示线上字段名为 `info`，但 runtime
`0x01026188`：`ldr r1,[pc,#0x64]; add r1,pc` 的字面量 `0xffffb63a` 解析到
字符串 **`iteminfo`**（`0x010217c8`），再 `blx [r4,#0x28]`。池旁虽有
`info\0`，成功路径并不引用它。

getter 找不到 `iteminfo` → 返回 NULL → `0x01033B16` 以空包装初始化流 →
`[NULL+4]` 读地址 4，与日志一致。`result`/`total` 名解析正常，故能走到自扣
与 stream init。

实现：`vm_net_mock_build_chest_open_info_blob` + 成功包字段名 **`iteminfo`**
（勿用 `info`；勿再调用带 rowCount 的 `vm_net_mock_build_item_use_iteminfo_blob`）。

说明：文档/IDA 常用 file-abs 地址（+`codeOffset 0x9a`）；runtime VA = 代码段
内偏移 + `0x01000000`。

## 权威状态与奖励池

- 宝箱 `524` + 钥匙 `815` 各消耗 1；同一事务内写入奖励后再 `role_db_save`。
- 奖励池与后台「物品管理」同源：`g_vm_net_mock_shop_catalog`（item.dsh/equip.dsh）。
- 下列来源合并为一个池，每次开启均匀抽取 **恰好一件**：
  1. 装备品质 1–3（equip.dsh `品质`）
  2. 商城密宝：非装备且分类 14（后台「秘宝道具」）
  3. 分类 10，排除 920/921（修炼天书走专用协议，不进池）
  4. 分类 21
  5. 分类 23（玄晶）
  6. 固定 835/836/837
  7. 分类 27

## 稀有奖励世界消息

成功开启且奖励属于下列任一条件时，服务端向所有在线且 presence 有效的会话投递
世界频道 `1/3/3`（type=0 / `[世]`），来源名「系统」，文案 GBK：

`恭喜{角色名}开启黄金宝箱获得{物品名}，祝贺！`

触发条件：

1. 装备 `quality==3`
2. 分类 23 玄晶且等级 ≥5（物品 905–916；一级=901）
3. 十倍经验卡：物品 811
4. 天书：物品 920/921，或商店名含「天书」

说明：

- 仅即时 fanout，不写入 `world_chat_messages`（避免刷屏污染登录历史）。
- 文案受 chat notice `message[82]`（≤81 字节）限制；超长则打 warn 并跳过广播。
- 当前奖励池仍排除 920/921，因此天书广播分支在池内无同名道具时不会从开箱触发；判定已按权威命名接好。
- 实现：`vm_net_mock_gold_chest_maybe_announce_rare_reward`（`mock_server_equipment_npc.c`），
  由 `vm_net_mock_build_chest_open_response` 成功路径调用。

## 修改点

- `mock_server_catalog.c`：`vm_net_mock_build_chest_open_response`；522–524/813–815 列入特殊协议，避免 `7/1` 误扣；奖励池品质 1–3；双 `x` 字段解析；`7/15` 禁止空响应。
- `mock_server_dispatch.c`：在特殊时效道具之前分派 `7/15`。
- `mock_server_equipment_npc.c`：品质 3 / 五级+玄晶 / 十倍经验 811 / 天书 世界祝贺广播。

## 验证

- [x] `make -j2`
- [ ] 有 524+815 时开启：扣各 1，获得上述池中一件，客户端提示「获得1个%s」；堆叠物不得提示原有+1；无 `pc=0x01033A68` 软失败；日志 `mock_chest_open ... granted=1`
- [ ] 不应再出现开箱后无 `mock_chest_open`、客户端一直转圈
- [ ] 抽到品质 3 装备、905–916 玄晶或 811 十倍经验卡：全服世界频道出现「恭喜…开启黄金宝箱获得…，祝贺！」
- [ ] 抽到品质 1–2 装备、四级及以下玄晶、双倍/四倍经验卡等：无上述世界祝贺
- [ ] 无钥匙：`result=3`，道具不扣
- [ ] 背包满且无法堆叠：`result=4`，宝箱与钥匙不扣
- [ ] 522/523 暂不实现掉落表时不得被 `7/1` 当普通消耗品删掉
