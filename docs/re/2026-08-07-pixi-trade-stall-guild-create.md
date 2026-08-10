# 2026-08-07 Pixi：完整交易栏 + 帮派创建

phase: pixi-trade-stall-guild-create
status: implemented-client

## Scope

- 交易完整链路 `21/1…21/8`：报价栏、对方镜像、确认/取消（非伪造成交）
- 帮派创建三阶段 `10/30 → 10/27`、`10/28`、`10/11`（菜单「创建帮派」）

服务端契约已在 mock 存在；本轮仅 Pixi 接入。

## Trade UI

| 阶段 | WT | Pixi |
|------|-----|------|
| 打开 | `21/4 result=1` | `TradePanel` phase=`trade` |
| 报价 | `21/5 {num,trademoney,iteminfo}` | 选背包 seq+数量 / 设金币 → 提交 |
| 对端刷新 | poll `21/6` | 右侧「对方报价」 |
| 确认 | `21/7 result=1` | 等待 → 双方确认后 `21/8` |
| 取消 | `21/7 result=2` | 关闭面板 |

`iteminfo` 报价行：tagged seq + tagged count（`SendTradeSellReq`）。
成功 `21/8` 后刷新背包；不伪造结算。

## Guild create

菜单「创建帮派」：

1. `1/10/30` → `10/27 result=1` → prompt 名称
2. `1/10/28 {name}` → `10/28 result=1,info` → dialog 确认
3. `1/10/11 {name}` → `10/11 result=1,id…`；失败 tip

已入帮时服务端回 `10/30 result=4`，本机 tip，不打开创建名。

## Files

- `protocolSocial.ts`：offer/confirm/guild-create builders + parsers
- `TradePanel.ts`、`GameSession`、`main.ts`、`MenuScreen.ts`
- 证据：`docs/re/2026-07-11-nearby-player-actions.md`、`2026-07-17-guild-list-and-role-display.md`

## Validation

- `npx tsc --noEmit`
- `npx tsx scripts/smoke-protocol.ts`
- 双端：放物品+金币 → 双方确认 → 背包/金钱变更；取消对端关窗
- 未入帮：菜单创建 → 名称 → 确认 → 人物属性帮派名更新

## Negative / unresolved

- 帮派列表/管理/审批 UI 未接（仅创建）
- 入帮邀请对端 `10/14` 仍 unresolved
- 勿用默认物品/金币掩盖空报价或失败 `21/8`
