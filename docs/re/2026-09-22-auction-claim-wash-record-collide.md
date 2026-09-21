# 拍卖领取空洗练被 wash_record 撞成红字（2026-09-22）

## 触发与现象

1. 后台 `first_seed_percent=0`（永不首获随机）。
2. 拍卖行详情：**无**洗练红字（listing `enhance_wash` 为空 / 无 bit7）。
3. 中拍或流拍后点领取 → 背包同件装备**立刻带洗练红字**。

## 业务链路

1. 上架：背包 `enhance_wash` 写入 `server_auction_listings`（可空）。
2. 成交/流拍：`insert_claim` 拷贝 listing 的 `enhance_wash` → `server_auction_claims`。
3. 领取 `auction-claim-item`：读 claim 行 → `return_item_to_bag`（带 wash 指针，空亦不重随）→ 7/7 入包。

## 首次偏离

领取前调用了：

```c
coalesce_from_record(claimerRoleId, claim.itemInstanceId, …, &claim.enhanceWash);
```

claim 行为空 wash 时，用**买家** `role_id` + **卖家** `item_instance_id` 查
`account_role_enhance_wash_record`。实例 id 是各角色独立计数（常从 1 起），
与买家自己洗过的旧装 id 碰撞后，把买家旧洗练快照填进拍品，再
`relocate_seq` 落成买家权威。

拍卖列表用的是 listing 行上的空 wash，故界面正确无红字；偏离只在领取。

证据：`jh_online` 当时 `first_seed_percent=0`，`item_instance_id∈[1,20]` 的
wash_record 仍有数十条，碰撞面很大。

## 根因

跨角色拍卖转移误用「同角色穿脱兜底」的 `coalesce_from_record`：
claim/listing 上的 `enhance_wash`（含空）才是转移权威；不能按领取者
role+instance 回填。

与首获概率无关：`add_backpack` 在 `wash!=NULL` 时不走 `seed_initial`。

## 修改

`src/server/mock_server_auction.c`：

1. 领取（winner / item-return）与下架退回：**去掉** `coalesce_from_record`。
2. 入包后：claim wash 非空 → `relocate_seq`；**空** → `enhance_wash_record_delete`
   （清掉本角色同 instance/seq 碰撞记录，避免之后穿戴再 coalesce 复活）。

## 验证

- [ ] 拍一件 listing 无红字的装备：领取后背包仍无红字（概率 0 环境）。
- [ ] 拍一件本就带洗练的装备：领取后红字与拍卖详情一致。
- [ ] 下架退回：空/非空 wash 与上架时一致。
- [ ] `make -j2`

## 仍未知 / 风险

- 历史上已被错误 coalesce + relocate 写进背包的件，不会自动清；需个案或扫库。
- 上架路径仍对**卖家**做 coalesce（同角色兜底）；若卖家 bag 空但 record 脏，
  listing 会带上洗练并在拍卖 UI 可见——与本次「领取才出现」不同。
