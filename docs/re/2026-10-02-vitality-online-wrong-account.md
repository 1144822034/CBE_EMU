# 在线活力满小时未增加

| 项 | 内容 |
|----|------|
| 症状 | 玩家在线满 1 小时，属性/节日面板活力不涨；后续反馈：系统提示已出，面板仍是 0 |
| 根因 1 | `vitality_online_tick` 遍历全部在线会话，但发放走 `vitality_add`→`account_hex()`，写的是**当前 scene-poll 的 active account**，不是 session 本人；满小时进度已被取模消耗 |
| 根因 2 | 提示按 session 发放后，面板 `vitality_wire` 用另一套账号拼写做 `UNHEX`。`accounts.account_id` 是 ascii_bin，登录可规范化大小写，堆上 state 仍可能是另一种大小写 → 发到 A 行、读到 B 行（0） |
| 连带 | `session_end` 对 `>=3600` 只取模不发放；发放失败仍落盘余数 |
| 修复 | `vitality_add_of` 按会话账号写入；tick/end/begin 共用 `try_grant`；失败保留整小时 bank；系统聊天提示；读写键走 `accounts` 规范拼写 + 角色在线 session 账号；INSERT 后回读；发放后校验回读；加载时合并同角色大小写孤儿行 |
| 验证 | 多开至少 2 客户端，A 累计满小时时由 B 触发 poll；日志 `vitality_online_grant … account=<A>`；A 重开属性/节日面板活力 +N。已提示过仍 0 的号：开一次属性应触发 `vitality_case_orphan_merged` 并把值显示出来 |

证据：原 tick 在 `hours>0` 后调用 `vitality_add(role)`，而 `vitality_load/save` 只用 active account。面板 0 + 系统提示并存：`try_grant` 已对 session 写成功并 tip，但 `1/1/21` / 节日 status 的 `UNHEX(account)` 与写入键字节不一致。
