# 「关于任务」接不上：白展堂被标成技能导师

## 环境

`192.1.1.3` / `lxh001` role=1。任务 1「初来乍到」已 `state=3`；任务 2「关于任务」无行。

## 症状与日志

```text
mock_npc_dialog actor=30002 name=白展堂 npc_kind=3 service_action=3825205249(=0xe4000001 OPEN_SKILLS)
  task_offer=1 task_option_action=4
mock_task action=detail task=2 request_subtype=10 ...  （详情成功）
随后只有空 25/5 → builtin-scene-default-event
没有任何 mock_task action=accept / 6/11
```

客户端在详情后没有发出接取包，表现为“一直接不上”。

## 根因

1. `server_dynamic_npcs` 把白展堂配成 `npc_kind=3`（技能导师）。按
   `docs/re/2026-07-19-dynamic-npc-services.md`，非零 kind 会在任务选项外再加
   `action=1` 服务入口；`action=1` 走 `26/1 {type=2,id=value}` 技能子菜单。
2. 旧对话序列把**服务选项排在任务选项之前**，与文档“在原有任务选项之外增加”
   不符。白展堂对话首项变成「学习技能」，任务「接受关于任务」在后；服务子菜单
   与任务大厅确认回调抢同一对话收尾，运行时只见 `6/10` 详情和孤立 `25/5`，
   **不见 `6/11` 接取**。
3. `role=0` 仅因 `6/10` 详情分支不取 active role，不是接取失败原因。库中前置
   任务 1 已完成，背包也有空位接收 `given_item_id=800`。

## 修改

- 对话序列改为：测试选项 → **任务选项** → 服务选项。
- XSE offer 同样写入 `task_offer_context`（与动态绑定一致）。
- 任务处理器额外接受前缀组合 `25/5 + 6/11`（与已有 `6/11 + 25/5` 对称）。
- 数据：`192.1.1.3` 上白展堂 `npc_kind` 改回 `0`。

## 验证

1. 部署新服务端并确认白展堂 `npc_kind=0`。
2. 点白展堂：首项应为「接受关于任务」，无「学习技能」。
3. 确认后日志出现 `mock_task action=accept task=2 result=0`，
   `account_role_tasks` 有 `task_id=2 state=1`。
