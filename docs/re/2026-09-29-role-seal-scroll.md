# 角色封印卷（道具 857）

日期：2026-09-29

## 概述

空白封印卷可封印同账号其它**离线**角色到托管账号 `__seal_escrow__`；绑定卷可上架藏宝阁，买家浏览可看封印角色面板/装备/背包/仓库，使用后整角过户到本账号。

## 协议

| WT | 含义 |
|----|------|
| `1/29/25 { cardseq, targetrole }` → `29/25` | 封印 |
| `1/29/26 { cardseq }` → `29/26` | 解封/过户 |
| 空白 `7/1` | 拒绝 tip「请选择要封印的角色」 |

封印 result：1 ok；2 卷缺失；3 目标无效；4 当前角；5 在线；6 同账号不足；7 非同账号；8 帮主未移交；9 扣发失败；10 过户失败；11 已绑定请解封；12 实例分配失败；13 存档失败。

解封 result：1 ok；2 卷缺失；3 空白卷；4 角色位满；5 过户失败；6 扣除失败。

## 数据

- 迁移：`server/mysql/migrate_add_role_seal.sql`（运行时 `vm_net_mock_role_seal_ensure_schema` 也会建表）
- `server_sealed_roles`：`item_instance_id` PK ↔ `sealed_role_id`
- 过户：`vm_net_mock_role_db_reassign_account`（FK 子表 + friendships，禁 delete）
- 2026-09-29 修复：过户曾把 `guild_applications` 当 `account_id`/`role_id` 更新，真实列为 `applicant_account_id`/`applicant_role_id`，UPDATE 必失败 → 封印/解封 `result=10/5`「角色过户失败」。现改为过户时 DELETE 申请行，detach 也用正确列名。
- 2026-09-29 修复：封印成功后先 `save_role` 再 `splice`，`note_durable_role_write` 把仍含封印角的 `roleDb` 打进账号缓存；sticky capture 跳过后续 memcpy → 下一请求恢复出已封印角与空白卷。改为先 splice 再 save。
- 2026-09-29 修复：上次半成功会把角留在 `__seal_escrow__`/`server_sealed_roles`，再封印因 `account-roles-miss`/`insert-sealed` 报过户失败。过户对已在目标账号幂等；封印复用已有 instance、跳过已 escrow、失败只回滚本次新建。
- 2026-09-29 修复：空白卷 count>1 且背包满时，consume+grant 无空位 → result=9「扣除/发放封印卷失败」。count==1 时改为就地写入 `enhanceInstanceId`。
- 2026-09-29 修复：封印后 scoped `save_role` 用压缩后的内存下标写 `role_index`，与同账号其它角的 `uk_account_roles_index` 冲突 → result=13「封印存档失败」。scoped 改为 UPDATE 且不改 `role_index`。
- 2026-09-29 修复：不改 `role_index` 导致 `role_count` 缩小后幸存角仍占高 index，加载 `role_index>=role_count` 整号失败 → 选角全空。加载改为按序密排；不匹配时自动修 meta/index；封印成功改 full snapshot 重编号。
- 2026-09-29 修复：使用绑定卷提示「角色过户失败」。解封写死从 `__seal_escrow__` 迁出，半失败时角色仍在原账号 → `account-roles-miss`；子表缺失也会整单回滚。现改为按实际 `account_id` 过户、已在本账号则幂等解封、缺失子表跳过，提交前校验归属。
- 2026-09-29 修复：`role-missing-db`——`server_sealed_roles` 仍绑 instance，但 `account_roles` 行已被 sticky 全量存档/CASCADE 弄丢。解封时用 preview 重建托管行再过户；sticky restore 后 purge 已绑定角；full save 跳过 `server_sealed_roles` 绑定 role_id。
- 2026-09-29 修复：重建后过户仍 `verify-owner`（want=买家 got=escrow）。子表 `account_role_tianxia_worship_daily` 无 `account_id`，UPDATE 报 1054；旧 mysql-client 把 SQL ERR 当断线关掉连接 → 未提交事务回滚。现：ERR 保连；该表移出 account_id 子表列表。
- 2026-09-29 修复：孤儿重建曾写入 ASCII `xinshoucun` 场景，解封后进图失败、列表像残缺角。现用默认蓬莱 `.sce`；封印预览增加 scene/pos；解封后若场景无 `.sce` 再治愈。已损坏的 lxh002 role=2 已改回蓬莱场景。

## 商城目录持久化

- 857 不在 `item.dsh`：旧 `ensure` 只注入不回绑 MySQL，重启把价格打成 1、`enabled=1` + category=14 易再次上架酷宝。
- 2026-09-30 修复：注入后 `SELECT` 回绑；无行 seed `enabled=0`。日志 `mock_role_seal_scroll_catalog_bound`。

## 藏宝阁

- 仅 **已绑定** 卷可上架（空白拒绝）
- browse/mine 附加 `sealedRole` 摘要
- `GET /api/zangbao/seal-detail?listingId=` 四栏只读快照
- `GET /api/zangbao/seal-detail?bagSeq=`（需登录）背包绑定卷详情；空白卷返回 `blank:true`
- 托管角色行缺失时回退 `server_sealed_roles` preview，避免详情空白
- 2026-09-29 修复：下架/购买退回背包后变成「空白封印卷」——`auction_return_item_to_bag` → `add_backpack` 把 857 当可叠加耐久道具：`memset` 新行未写 `enhanceInstanceId`，或叠到已有空白卷上冲掉绑定。现：`enhanceInstanceId!=0` 单独建绑定行并写回 instance；空白卷只叠其它空白行。购买结算同路径一并修复。
- 2026-09-29 数据修复：`lxh002` role=4 slot=2 空白 857 已按 cancelled listing#2 写回 `item_instance_id=45558`（封印角 role=2）；`server_sealed_roles` bag_hits 恢复为 1。匹配条件：同账号同角色、背包仅一张 instance=0 的 857、且存在对应 cancelled listing+sealed 行。
- 2026-09-29 修复：背包封印后详情仍显示「空白封印卷」——客户端未拉封印角色摘要。现：`onEnrichBagDetail` + 背包刷新异步 enrich；列表名 `[印]角色`；封印成功立刻本地盖章；藏宝阁在仅有 `itemInstanceId` 时也拉 seal-detail
- 2026-09-29 完善：面板显示 **酷宝(wcoin)/酷币(kcoin)**；装备/背包/仓库行带 `enhanceWash`+宝石，客户端可单独打开装备/道具详情（上下选、确认查看）
- 2026-09-29 完善：seal-detail 优先加载托管角内存态并 `build_player_stats`，面板字段对齐角色面板（称号/配偶/帮派/魅力/三围/物攻护甲/命中躲闪暴击抗性/宝石%/活力/经验曲线），另附金钱·酷宝·酷币
- 2026-09-29 修复：面板属性全 0——`__seal_escrow__` 不在登录账号表，`open_account_role_db` 失败回退 SQL 零值。改为 MySQL 直接装 scratch role（含装备洗练/宝石）再 `build_player_stats`

## 客户端

- 使用：先 `29/26`；result=3 再选角 `29/25`
- 解封成功：`openBag` 后再按 `remaining` 强制扣卷（防 sticky 仍画出已用卷），并刷新背包 UI / 元数据
- 背包详情：绑定卷显示已封印角色名/等级/职业；空白卷仍用目录说明
- `ZangbaoPanel` 详情 Tab：面板/装备/背包/仓库；装包仓可进单件详情

## 关键文件

- `src/server/mock_server_role_seal.inc.c`
- `src/server/mock_server_dispatch.c` / `equipment_npc.c`
- `src/web_zangbao.inc.c`、`PixiJS_web/scripts/ws-gateway.mjs`
- `PixiJS_web`：`protocolExtras.ts`、`GameSession.ts`、`main.ts`、`zangbaoApi.ts`、`ZangbaoPanel.ts`、`SystemPanel.ts`

## 复测

1. 两角账号封印其一 → 选角只剩 1；卷可上架
2. 封印成功后打开背包卷详情 → 显示「已封印：角色名 Lv…」且标题 `[印]角色名`
3. 买家看 seal-detail 四栏 → 购买 → 使用解封 → 多一角且装/仓/包完整
4. 满 5 角解封失败；目标在线/封印当前角拒绝
5. 空白卷不可上架；改名卡/普通藏宝阁回归
6. 绑定卷上架 → 下架 → 背包仍为 `[印]角色`（非空白）；有空白卷同背包时下架也不叠坏
7. 绑定卷上架 → 他号购买结算进包 → 仍为绑定卷可 seal-detail / 解封
