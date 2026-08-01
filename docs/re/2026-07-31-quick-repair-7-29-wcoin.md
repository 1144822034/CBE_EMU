# 快速修理 7/29（酷宝）

## phase

协议补齐；状态：已实现（两阶段报价 + 确认）。

## 触发与运行时证据

死亡/个性功能菜单「快速修理」发送：

```text
unhandled wt=7/29 len=19 objects=1 first=1/7/29:10
```

## 客户端契约

| 项 | 证据 |
|----|------|
| 报价请求 | `JianghuOL.CBE:0x0101CADC` → `1/7/29 {type}`；`type=2` 另带 `seq`、`id` |
| type=1 入口 | `0x0102CE9E`（菜单确认后 `movs r0,#1; bl 0x0101CADC`） |
| type=2 入口 | `0x0101D15A`（单件，耐久未满时） |
| 报价解析 | `HandleRepairResponse/0x01028C9A` 读 `type`、`repairnum`、`coolmoney` |
| repairnum>0 | 弹出 W 币确认「您身上有 N 件…花费 M 币」；**N=`repairnum` 件数，M=`coolmoney` 费用** |
| repairnum≤0 且 type∈{1,2} | 提示「没有装备需要修理!」/「这件装备不需要修理!」 |
| 确认请求 | 确认后 `JianghuOL.CBE:0x0101A8A0` → `1/7/13 {type/flag/seq/id…}` |
| 完成解析 | `mmGameMstarWqvga.cbm:0x4986`：`1/7/13 {result}`；`result=1` tip「修理完成」并清 wait；`result=2` 资金不足 |

NPC 铜钱全修仍走 `26/1` + `vm_net_mock_role_service_repair_all`，与本包无关。

## 根因备忘（「您身上有25455件装备」）

`repair_cost` 返回的是**缺失耐久铜钱和**（可≈25455）。若把该值写入 `7/29.repairnum`，客户端会当成件数显示「25455件」。报价字段契约：`repairnum`=待修件数，`coolmoney`=酷宝费用。件数必须直接扫装备槽位耐久缺口统计，不要把 `repair_cost` 的返回值当件数。

## 玩法契约（本任务）

- **报价 `7/29`**：`repairnum`=待修件数，`coolmoney`=5；不扣费、不改耐久；session 记下 quote。
- **确认 `7/13`**：扣 5 酷宝并修耐久；成功 `result=1`，酷宝不足/失败 `result=2`；装备 `7/7` poll 延期。
- 无需修理：回显请求 `type` + `repairnum=0` 的 `7/29`，无 `7/13`。
- 不要用系统聊天 + `7/29 type=0` 冒充完成态（会卡进度条，见 `2026-08-01-quick-repair-progress-stuck.md`）。

## 修改点

- `mock_server_catalog.c`：`repair_all_free` / `repair_one_free`（`repair_cost` 的 count 输出作件数）
- `mock_server_equipment_npc.c`：报价 `build_equipment_repair_response` + 确认 `build_equipment_repair_confirm_response`
- `mock_server_scene_sync.c` / `mock_server_social.c`：poll `quickRepairEquipSync`
- `mock_server_dispatch.c`：`builtin-equipment-repair` + `builtin-equipment-repair-confirm`

## 验证

1. 有耐久缺口且酷宝≥5：点快速修理 → 确认文案件数合理（非铜钱和）且花费 5 → 确认后 tip「修理完成」、进度条消失、酷宝-5、耐久满。
2. 酷宝<5：确认后资金不足对话框、进度条消失、耐久与酷宝不变。
3. 无需修理：客户端「没有装备需要修理!」。
4. NPC 欧冶子铜钱修理路径不受影响。
