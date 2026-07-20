# 任务提交结果与 NPC 头顶标识

## 结论

- 正常任务提交是上行 `1/6/4 {taskid}`、下行 `1/6/4 {result=1,...}`。
- 下行 `1/6/16` 属于任务重置结果。客户端成功分支在
  `HandleTaskCompleteResult(0x01038E6E)` 显示硬编码 GBK 文本“重置成功!”，
  因此不能用于普通提交。
- `scene_refresh_interact_prompt_types(0x01017C6C)` 用 NPC 节点 `node+68`
  的名称做精确匹配：
  - `6/14` 候选任务第二个字符串匹配时写 `node+326=2`，即正常感叹号；
  - `6/1` 活动任务的交付人字符串匹配且状态为 1 时写类型 3，即灰色问号；
  - 同一活动任务状态为 2 时写类型 1，即正常问号。

IDA 实例通过 `binary_name=江湖OL.CBE` 动态选择，未在实现或文档中固定实例 ID。

## 客户端证据

`net_handle_task_response_dispatch(0x0104726C)` case 4：

1. 读取 `result`；0 显示“任务提交失败”，1 进入成功路径。
2. 成功路径读取 `energy/energymax`，并由
   `HandleLevelUpResponse(0x01046EDA)` 读取
   `exp/level/lastexp/curexp/persentexp`。
3. 读取 `seqnum/iteminfo` 和可选 `awardinfo`。
4. 读取原始字段 `taskdes`，移除活动任务并调用
   `scene_refresh_interact_prompt_types`。

地址 `0x0104722C` 的字节为 `65 78 70 00`，确认字段名是 `exp`；
`0x01038F8C` 的字节为
`D6 D8 D6 C3 B3 C9 B9 A6 21`，GBK 解码为“重置成功!”。

## 服务端修正

- 提交成功响应改为 `6/4 {result=1}`，下发当前经验、等级、活力、经验区间、
  `seqnum=0`、安全的空 `iteminfo`、奖励金额流和 `taskdes=任务提交成功！`。
- 动态 NPC 绑定任务后，活动任务记录会先查当前场景是否存在原始交付人；若不存在，
  则把该任务实际绑定的动态 NPC 显示名写入活动任务交付人槽。这样候选、进行中和
  已完成三阶段都能与同一个场景节点名称匹配。
- 场景资源组合函数显式接收目标场景，传送响应不再用切换前的当前场景生成 `6/14`。
- 非空 `27/11` NPC 目录下发后为当前会话挂一次任务提示刷新；下一次场景同步轮询
  独立补发 `6/1 + 6/14`，避开 NPC 节点视觉激活晚于同一网络分派尾部的时序窗口。

## 回归

- `make -j2` 通过。
- `php tmp/task-commit-combo-regression.php 19090`：真实
  `25/5 + 6/4` 复合请求得到 `25/5 + 6/4`，响应长度 224，不含“重置成功”。
- `php tmp/task-prompt-state-regression.php 19090`：任务 19 动态绑定 NPC“李四”，
  依次验证可接取类型 2、进行中类型 3、已完成类型 1，以及独立延后刷新。
- `php tmp/scene-npc-roundtrip-regression.php 19090`：离开并返回动态 NPC 场景、
  直接完整加载两条 NPC 生命周期均通过。
- `php tmp/task-abandon-regression.php 19090` 通过。
- 所有 MySQL 测试任务状态在 `finally` 中恢复；正式日志
  `tmp/mock-service-19090.task-prompts.20260720-145704.stderr.log` 为 0 字节。

