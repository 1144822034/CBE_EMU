# 副本向导「可接取任务」前置任务未拦住

## 触发

梦境 `29梦境空间_01` 挑战 BOSS（actor `32901`）：

- 绑定可接取任务 `100005`（`server_tasks.prerequisite_task_id=100004`）
- 同 NPC 还开着副本向导传送／挑战

前置 `100004` 未提交（甚至未接）时，对话仍像可接取：接取对白被换上，且
`lxh001/role=1` 实测在 `100004` 尚无完成行时就写入了 `100005 state=1`
（`100005` 接取时间早于 `100004`）。

## 根因

1. **接取对白未跟选项门闩对齐**：绑定任务在 `state==0` 时无条件换成
   `offerDialog`，即使 `vm_net_mock_task_definition_available()` 因前置未
   `state=3` 已拒绝添加「接受」选项。副本向导同时还有传送／挑战入口，玩家会
   感觉「前置没完成也能显示接取」。
2. **前置判定只信本次请求的状态快照**：`available()` 仅在
   `account_role_tasks` 列表快照里找前置行。快照按 `updated_at` LIMIT，历史很长
   时可能漏掉已完成前置；接取路径也依赖同一函数，需要权威单行读取兜底。

## 修改

- `available()`：快照里已是 `state=3` 则通过；否则对
  `prerequisite_task_id` 做一次 `task_state_load` 权威确认。
- 绑定任务对话：只有 `canOffer==true` 才追加「接受」并切换 `offerDialog`；
  前置未完成时保留 idle／服务对白，并打
  `mock_npc_dialog_task_prereq_gate`。
- 接取拒绝时打 `mock_task_accept_prereq_gate`。

## 验证

- `make -j2`
- 角色无 `100004 state=3`：点 `32901` 不应出现「接受…」，对白为「需要先完成前置任务。」
  （仍可看到副本传送／挑战分项）；伪造 `6/11 task=100005` 应失败并见
  `mock_task_accept_prereq_gate`（含 `reject-insert-until-prereq-state3`）
- 提交完 `100004` 后：应出现接取项，接取成功

## 2026-08-05 加固

- `available()`：快照漏掉本任务行时**不再**在此函数内 `task_state_load`（会在 NPC
  优先级 / `6/14` 上形成 N+1，表现为对话后卡死）；本任务权威校验改到 `6/11`。
- `task_accept()`：INSERT 前再权威校验 `prerequisite_task_id` 必须 `state=3`，堵住伪造 `6/11`。
- 前置未完成时对话改为显式前置提示，避免与接取对白混淆。

## 2026-08-05 「挑战守关怪」与任务名混淆

### 现象

`32901` 绑定 `100005`（名「挑战守关boss」，`prerequisite_task_id=100004`）。
`100004` 未 `state=3` 时，根对话仍出现「挑战守关怪」（EC）。玩家把该服务项读成
任务入口，报告「任务前置没生效」。

### 契约修正

同 NPC 若绑定任务且 `prerequisite_task_id!=0`：未完成该前置前**隐藏**「挑战守关怪」。
「接受…」仍只由 `available()` / `canOffer` 控制；传送仍只看
`teleport_require_task_id`。

### 验证

- 角色无 `100004 state=3`：点 `32901` → 无「接受挑战守关boss」、无「挑战守关怪」；
  对白「需要先完成前置任务。」；日志 `mock_npc_dialog_challenge_prereq_gate` /
  `mock_npc_dialog_task_prereq_gate`
- 完成 `100004` 后：两项可出现（传送仍受 `teleport_require_task_id=100005` 约束）

## 2026-08-05 每日链：当天完成 + 次数档对齐

### 契约

当当前任务或前置任务任一 NPC 绑定 `daily_accept_limit>0` 时，前置不再只看历史
`state=3`，还要同时满足：

1. **当天**：前置 `task_state=3` 且 `updated_at` 落在服务器本地自然日
   （与 `vm_net_mock_local_day_ymd` / 日接取表同一日界）。
2. **次数档**（前置绑定也配置了 `daily_accept_limit>0` 时）：
   `SUM(accept_count)`（当日、按 `task_id` 跨 NPC）满足
   `prereq_count >= current_count + (当前任务进行中?0:1)`。
   例如日限 3 次：第 2 轮接 `100005` 前，必须当天已第 2 次接取并完成 `100004`；
   仅靠昨天的 `100004=3` 或第 1 轮完成记录不够。
   前置未配日限时只校验「当天完成」（计数表不会为该绑定自增）。

`daily_accept_limit==0` 的剧情链仍只要求任意时间的前置 `state=3`。

运营：每日链两端（如 `100004` 与 `100005`）都应配置相同的 `daily_accept_limit`。

传送门闩 `teleport_require_task_id`：**不**套用当天/次数档（进梦境易被误伤）；
仍只要求目标任务历史 `state=3`。当天+次数只约束绑定任务前置链
（接取 / 「挑战守关怪」）。

### 日志

- `mock_task_prereq_day_gate`（缺当天完成）
- `mock_task_prereq_slot_gate`（次数档不对）
- `mock_task_accept_prereq_gate evidence=reject-*-prereq-day-slot`

### 验证

- 日限=3：只完成过昨天的 `100004` → 今日不可接 `100005` / 无「挑战守关怪」
- 今日第 1 次完成 `100004` → 可接第 1 次 `100005`
- `100005` 已交 1 次且未再做 `100004` → 不可接第 2 次；再完成一轮 `100004` 后可接

## 2026-08-05 进梦境 / 点 31000 画面卡住

### 证据（lxh001）

对话后 prompt 刷新：`mock_task_prereq_day_gate`×N、`candidate_daily_gate
actor=31000 task=100004`、`task_prompt_refresh_deliver`、poll resp=77，随后
`idle-timeout`。无继续操作。

### 根因

1. `prereq_satisfied` 用 `binding_lookup`→`collect` 查日限：在 `available()` /
   priority / `6/14` 热路径上重复读 SCE，对话后 prompt 刷新拖死主线程。
2. 日限/前置把接受与挑战都藏掉且又无传送项时，`26/1` option-count=0，客户端
   对话无法点选，表现为卡住。

### 修复

- 日限改为 `SELECT MAX(daily_accept_limit) FROM server_dynamic_npc_tasks`。
- 快照已有完成日且非今天 → 直接 false，不再权威 SELECT。
- option 全空时补「离开」项。
