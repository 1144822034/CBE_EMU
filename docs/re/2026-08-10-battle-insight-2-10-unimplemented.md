# 2026-08-10：战斗心得后提示「功能暂未实现(1/2/10)」

phase: battle-insight-compound-2-10  
status: fixed

## 现象

使用战斗心得（828）后聊天出现系统提示 `功能暂未实现(1/2/10)`。

## 根因

`1/25/6` 成功后 CBE 会再发 wait-clear `1/25/7`，并常把挂起的 `1/2/10` actor-other 刷进同一 WT（与丢弃 `7/4+2/10`、聊天 `3/1+2/10` 同型）。

原 `vm_net_mock_parse_special_item_seq_request` 要求**整包只有一个**特殊道具对象；`2/10+25/7` / `25/7+2/10` 解析失败。独立 combo 候选表又未收录 `25/6`/`25/7`，包落到 `unimplemented-handler-notice`，tip 取首对象 → `(1/2/10)`。

挂机按钮的 `2/10 Type=2 + 25/3` 不是本问题；用户反馈对应吃心得路径。

## 修改

| 文件 | 改动 |
|------|------|
| `mock_server_catalog.c` | 特殊道具 seq 解析：允许旁路 `1/2/10`；`25/6`/`25/7` 成功时附带空 `2/10` ACK |
| `mock_server_interaction_login.c` | independent-combo 候选增加 `25/6`、`25/7`（不含 `25/3` 挂机标记） |

## 验证

1. 背包使用 828：应见心得生效 tip / 图标，**无**「功能暂未实现(1/2/10)」
2. 服务端日志：`mock_special_item_use request=25/6` → `mock_battle_insight_followup request=25/7`（或 `builtin-independent-combo` 拆包）
3. 挂机 `Type=2+25/3` 仍走 `builtin-hangup-battle-start`
