# 付费传送阵 WT 2/9（酷宝=W币）

Date: 2026-07-25

Status: implemented (server) — portal-enter fix 2026-07-25

## 问题

踩 `c04临安府_02.sce`「景灵宫」等付费传送阵后卡住：

```text
unhandled wt=2/9 len=49 objects=1 first=1/2/9:40
source=ignored-unhandled-server-only response=0
```

## 请求证据（运行时）

```text
mock_paid_portal_request_fields ... exitID=9 mapID=23蟠龙寨_01.sce
payload_hex=056d617049440011000f3233f3b4c1fad5af5f30312e736365066578697449440006000400000009
```

| 字段 | 值 |
| --- | --- |
| `mapID` | 目标 `.sce` 字符串 |
| `exitID` | u32（景灵宫=9） |

## 失败探测与根因

| 尝试 | 结果 | 结论 |
| --- | --- | --- |
| 无响应 | 卡住 | 必须处理 `2/9` |
| `2/9{result=1}+30/1` | 闪退 | case 9（`0x010129F2`）读 `othernum/otherinfo`，不是 `result` |
| 空 `2/9+30/1` | 不闪退但卡死，无后续请求 | 成功路径不能带 leading `2/9` |
| 对照 | NPC 副本 / deferred portal：`resources?+30/1` | 切图与普通传送门进图相同，仅多扣酷宝 |

## 契约

| 项 | 内容 |
| --- | --- |
| 请求 | 单对象 `WT 2/9`：`mapID` + `exitID` |
| 货币 | 酷宝 = `role->wcoin` / `coolmoney` |
| 成功 | `resource-followup + 30/1 {scene,posinfo}`（与传送门进图同族），扣 W 币 |
| 失败 | 空 `1/2/9`（清 kind-2 等待，不进图） |
| 出生点 | 优先 `exitID` → 目标 SCE entry；否则 reasonable spawn |
| 费用 | 默认 10；`CBE_PAID_PORTAL_WCOIN_COST` |

## SCE 证据

| 源场景 | 显示名 | 目标 | 门槛文案 |
| --- | --- | --- | --- |
| `c04临安府_02.sce` | 景灵宫 | `23蟠龙寨_01.sce` | 25以上 |
| `09华山_03.sce` | 莲花峰 | `25华山洞窟_01.sce` | 35以上 |
| `c14蜀山_*` / `16锁妖塔_*` | 同类命名传送 | SCE 内场景名 | — |

## 实现

- `mock_server_scene_task.c`：`vm_net_mock_build_paid_portal_confirm_response`
- `mock_server_dispatch.c`：`builtin-paid-portal-confirm`

## 验证

- `make -j2`
- 复测景灵宫：`response=resources+30/1`，进入 `23蟠龙寨_01.sce`，有后续 scene follow-up
- 余额/等级不足：`response=2/9-empty`，不进图
- 回归：`2/1` moveinfo、edge `2/3` 传送

## 仍未知

- `exitID` 在目标 SCE 上的 entry 语义是否始终等于出生点
- SCE 内权威费用字段未钉死；当前默认 10
