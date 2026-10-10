# CBE 背包列表 921 已注满天书不显示名称

Date: 2026-10-10

## 现象

CBE 客户端背包列表中道具 **921**（修炼天书实例，含已注满）不显示物品名；详情仍可走 `1/7/38` 拉 `bookdes`。

## 根因

1. 背包行名称在 `item+4`，**16 字节** GBK 槽（`+20` 为 price；`backpack_parser` 取证）。
2. `item.dsh` 921 有名称「修炼天书」，但 **说明为空**；登录靠 `30/21`→`BuildTimerItemData`，部分路径下 `item+4` 未填或被清掉 → 列表空名。
3. 开背包上行常为 `12/1+7/42+25/5×N+6/1+6/13+6/14`（无 `2/10`）。mock 旧检测拒 `12/1` / 不认 len=54 → **`功能暂未实现(1/12/1)`**，且无 `17/1` 补名。
4. Pixi 长标题超过 CBE 16 字节槽，不适合直接写入列表名。

## 修改

| 位置 | 行为 |
| --- | --- |
| `mock_server_interaction_login.c` | 识别上述 HUD 组合包 → 回 `12/1+7/42+17/1` |
| `mock_server_dispatch.c` | `builtin-cbe-bag-open-skill-combo`，避免 unimplemented tip |
| `main.c` `BuildTimerItemData` / `ProcessItem` / 绘制 | 920/921 强制 `item+4` 原名 |
| `main.c` `0x05184538` / `0x05184498` | `17/1` 提交后写 UI 列表 `r9+10680` |
| `network-client.c` `17/1` truncate | copy-safe 截断时**优先保留 920/921** |
| `network-client.c` 上行/下行 `7/38` | 下行后扫列表写原名 |

## 续记：VM pool 无 CODE hook（同日）

全局 `UC_HOOK_CODE` 排除 `0x05xxxxxx`（Android free-storm），故原 `0x05184538` / `0x05184498` 强制写名未触发；开背包 `17/1` 后 UI 行仍空。

补：窄 hook 两地址 + `17/1` 下行后 host pump 强制 `item+4`。详见 `2026-10-10-cbe-login-slow-npc-chat-921.md`。

## 续记：manager 有名 / UI 列表仍空（同日）

`client_out` 已有 `cbe_cultivation_book_list_name item=921`（ProcessItem/manager），但列表仍空。  
`17/1` 在 mmGame **另建** `moduleR9+10680` 行数组；只写 Global_R9 manager 不够。

再补：遍历 `g_dlSpBf` / `g_currentScreenModuleBase` / 全部 DL `spBf` 写 UI 列表；commit 后至 `0x05184580` 窄 hook；`17/1` 后 pump×48；日志 `cbe_cultivation_book_ui_list … blank_before=`。

## 续记：DSH 说明 + 动态基址 hook（同日）

证据：`item.dsh` 921 **名称有、说明为空**（`\x00`）；固定 `0x0518*` 窄 hook 未打出 `ui_list`（mmGame 在 VM pool 内重定位）。

1. 四份 `item.dsh`（`bin/` / `web/fs/` / JianghuOL assets / Pixi assets）将 921 说明补为「修炼天书」，并更新该行 `rowLen`（+8）。
2. `main.c`：按 `+0x4498/+0x4538` Thumb 签名在 pool/`buffer` 上定位 mmGame，动态 `uc_hook_add`；PC 匹配用 `base+off`；`17/1` 时 one-shot 扫 pool。日志：`cbe_mmgame_book_hooks base=…`。

## 续记：改写下行 17/1（同日）

`17/1 iteminfo` **无名称字段**（仅 `itemId + compact extra`）；列表名靠本地 DSH。  
运行证据：`cbe_mmgame_book_hooks base=05025048` 已挂上，但仍无 `ui_list`（commit 只匹配 4 个绝对 off，重定位后未命中）。

1. `network-client.c` 改写 `17/1` 时将显示用 `921→920`（920 说明非空；`30/21` 仍保留真实 921）。日志：`cbe_17_book_display_alias`。
2. commit 窄 hook 改为匹配整段 `base+0x4538..+0x4580`，在 DSH 填完后强制 `item+4`。

## 续记：VM pool 扫描补名（同日）

别名 + hook 仍无 `ui_list`（`r9+10680` 在 pump 时经常为空）。  
补：`17/1` 后对 `RAM_MEMPOOL` 按 4 字节步进找 id=920/921 且 `price+20` 合理的 324B 行，直接写 `item+4`。  
日志：`cbe_cultivation_book_pool_scan` / `cbe_cultivation_book_commit_pc`。

## 验证

1. `make -j2`，重启 CBE。
2. 开背包：应见 `cbe_cultivation_book_pool_scan patched>0`；列表显示「修炼天书」。
3. 详情 `7/38` 仍可用。
