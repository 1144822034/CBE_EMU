# 拍卖行后台开关

日期: 2026-09-11（客户端状态栏 2026-09-12 补齐）

## 需求

后台管理可开启/关闭拍卖行；Pixi 地图角「拍」图标随 `enabled` 显示/隐藏。

## 实现

### 配置

- 表 `server_auction_config`（`id=1`, `enabled`）原本已有，默认开启。
- 后台 Tab「拍卖行」：`src/web_admin_auction.inc.c`，动作 `save-auction-config`。
- 保存：`vm_mock_auction_admin_save_config` → `INSERT … ON DUPLICATE KEY UPDATE enabled`，并强制重载内存配置。
- status（42/1）原本就会下发 `enabled`。

### 关闭时服务端契约

| 操作 | 关闭后 |
|------|--------|
| 上架 create | 拒：`message=disabled`（原本已有） |
| 出价 bid / 一口价挑战 / 一口价 | 拒：`message=disabled`（本次补齐） |
| 浏览 / 我的 / status | 仍可用 |
| 撤拍 cancel / 领取 claim / 待领列表 | 仍可用 |
| tick 到期结算 | 仍照常 |

客户端 tip 映射：`disabled` →「拍卖未开放」。

### 客户端「拍」徽章

- `SceneView.setPaiVisible` + `layoutStatusBadges` 跳过隐藏的「拍」。
- `main.syncAuctionPaiBadge(status)`：进图拉 42/1、打开拍卖面板、领取结果、拍卖事件后同步。
- 显示条件：`enabled || pendingClaims > 0 || blink`  
  （关闭拍卖后仍保留待领入口，与服务端「claim 仍可用」一致；无待领则隐藏。）

## 验证

1. 后台「拍卖行」→ 关 → 保存；进图后「拍」消失；上架/出价/一口价返回 `disabled`
2. 关闭前已有待领：`blink`/pendingClaims 时「拍」仍显示，可 claim
3. 后台开 → 「拍」出现，可正常上架与竞拍
4. 领取清空后若仍关闭 → 「拍」再次隐藏
