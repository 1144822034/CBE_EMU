# 快速修理：报价件数、确认结算与系统反馈

日期：2026-08-01

Status: implemented

## 现象

1. 确认框显示「您身上有25455件装备」（件数≈字段错位读到的 `coolmoney` 名）。
2. 确认后耐久已修、酷宝已扣，但 mmGame tip「修理完成」不可见。

## 契约

| 阶段 | 包 | 字段 |
|------|----|------|
| 报价 | `1/7/29` | `type` u8；`repairnum`/`coolmoney` **u32**（件数 + 费用 5） |
| 确认请求 | `1/7/13` | 可能带 type/flag=2；操作以 session 报价 type 为准 |
| 确认响应 | `1/7/13 {result}` u8 + 同包 `26/0` | result=1 成功；result=2 失败 |
| 反馈 | 系统聊天 | 成功「修理完成」；酷宝不足另发提示 |
| 装备同步 | poll 延后 `7/7` type2 → `26/0` | 约 1s 后，避免冲掉完成反馈 |

## 根因

1. `repairnum`/`coolmoney` 用 u16 时，HandleRepairResponse 错位把 `"co"`（0x636F=25455）当成件数。
2. 确认包 type/flag=2 若覆盖报价 type=1 且无 seq，两个修理分支都不进。
3. mmGame tip 不可靠；用系统消息补可见反馈，仍保留 `7/13`+`26/0` 清 wait。

## 修改点

- `mock_server_equipment_npc.c`：报价 u32 字段；确认以 quote type 为准；`result` u8 + 同包 `26/0`；系统聊天。
- `mock_server_scene_sync.c`：装备 sync 延后约 1s。

## 验证

1. 有缺口：确认框件数合理、花费 5；确认后系统消息「修理完成」、酷宝-5、耐久满。
2. 酷宝不足：系统消息「酷宝不足…」。
3. 无需修理：原 tip；无 `7/13`。
