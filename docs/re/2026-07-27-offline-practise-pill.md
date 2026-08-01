# 修炼丹离线修炼结算

## 需求

1. 使用 827 修炼丹累积离线修炼时间（可叠加，有上限）。
2. 下线记录 `last_logout_unix`。
3. 上线按 `min(离线分钟, 修炼丹累积分钟, 当日剩余额度)` 结算经验：
   `exp = 结算分钟数 × 角色等级 × 8`。

## 资源契约（item.dsh 827）

- 每颗 +1 小时（60 分钟）修炼时间
- 累计银行上限 100 小时（6000 分钟）
- 每日可修炼上限 8 小时（480 分钟）
## 协议

- 使用：`1/7/16 {itemseq}` → `1/7/16 {result,maxnum,iteminfo}`；`result=1` 客户端自行扣数量
- Follow-up（运行时证实）：成功后客户端立刻发 `1/7/17`（含 `num`/`itemseq`）。
  服务端回 `1/7/17 {result=1,maxnum=0,iteminfo=""}` 结束等待，**不再次扣丹**。
  证据：`unhandled wt=7/17 len=38` 在 `builtin-practise-pill-use` 之后出现并卡住。

## 权威状态

表 `account_role_offline_practise`：

| 字段 | 含义 |
| --- | --- |
| `bank_minutes` | 修炼丹累积剩余分钟 |
| `last_logout_unix` | 最近一次下线墙钟时间；上线结算后清 0 |
| `today_ymd` / `today_used_minutes` | 当日已用修炼分钟（本地日） |
| `last_settle_exp` / `last_settle_minutes` | 最近一次上线结算，供 7/18 `getexp` |
| `is_gold` | 黄金修炼开关（7/21 持久化；本轮结算公式不乘双倍） |

## 流程

1. **使用**：扣背包 1 颗 → `bank_minutes += 60`（封顶 6000）→ `7/16 result=1`
2. **下线**：`session_mark_offline` 写入 `last_logout_unix=now`
3. **选角上线**：`settle_minutes = min(offline, bank, daily_left)`；加经验；扣银行与当日额度；清 logout；结算文案以系统消息（`1/3/3 type=5`）下发，不再走空的 `practiseflag/practiseinfo` 弹框
4. **7/18**：按库内真实剩余/已用/上次经验回填面板字段

## 实现入口

- `vm_net_mock_role_use_practise_pill`
- `vm_net_mock_role_offline_practise_mark_logout`
- `vm_net_mock_role_offline_practise_settle_on_login`
- `vm_net_mock_build_practise_pill_use_response`
- migrate：`server/mysql/migrate_add_offline_practise.sql`

## 验证

- [ ] `make -j2`
- [ ] 使用修炼丹：背包 -1，修炼面板累计时间 +1h
- [ ] 下线若干分钟再上线：经验增加 = min(离线,银行,当日剩余) × 等级 × 8
- [ ] 银行不足时按银行结算；离线不足时按离线结算
- [ ] 当日超过 8 小时后不再结算，次日恢复额度
- [ ] 银行满 100h 后再用失败且不扣丹
