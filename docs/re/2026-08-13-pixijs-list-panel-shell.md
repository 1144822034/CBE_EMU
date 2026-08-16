# PixiJS ListPanelShell

## 目的

统一 CBE 风格列表面板的外框、标题、行高亮与「上一页 / 下一页」，避免 Task / Social / Arena 各自复制一套分页与 hit 逻辑。

## 模块

`PixiJS_web/src/ui/listPanelShell.ts`

- `paintListShellChrome` — 面板底 + 标题胶囊
- `paintListShellTabs` — 等宽标签条（可选）
- `paintListShellBody` — 行绘制 + 本地/服务端/无分页
- `listShellPageInfo` / `listShellStepPage` — 游标分页计算

`PixiJS_web/src/ui/pageNav.ts` — `attachPageNavButtons`（可点上一页 / 下一页）

## 已接 ListPanelShell

| 面板 | 用法 |
|------|------|
| TaskPanel | chrome + 双行列表 local 分页 |
| SocialPanel | chrome；附近/队伍等 local；好友/恩怨 server；等级/PK 榜 local；财富榜仍自定义金元宝列 |
| ArenaPanel | 榜行 body（server 切片 + listScroll）；底栏动作行仍自绘 |

## 已接 pageNav 按钮（多页时）

- SystemPanel：背包 / 商城 / 状态 / 替换装备
- EnhancePanel：玄晶列表、转移目标
- TradePanel：配置背包
- AuctionPanel：上架选背包
- DialogPanel：NPC 长选项
- BattleScreen：仙术 / 物品菜单
- GuildPanel：帮派列表 / 成员 / 招收（按钮条，替代原两行菜单项）
- 拍卖浏览 / 赏金擂台：仍为列表内「上一页/下一页」动作行（服务端翻页）

## 刻意不纳入 shell v1

背包 / 商城图标格、交易双方栏等数据形态差异大的界面（仅加 pageNav）。

## 验证

- `npx tsc --noEmit`（PixiJS_web）
- 任务 / 附近 / 好友 / 比武 / 背包 / 强化 / 交易 / 对话 / 战斗技能 翻页
