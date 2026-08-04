# 任务接取确认对话框显示异常（占位 goal）

日期：2026-08-03

状态：已修复（`make -j2` 验证）

## 触发

梦境绑定任务（如 `100004` / `100005`，actor `31000` / `32901`）点「接受」后，
任务大厅确认页（`6/10 info`）显示 `-`、`-\n-` 或 `-\n奖励…`，玩家描述为乱码。

## 证据

`server_tasks` 实测（GBK 落库正确，并非 UTF-8 串码）：

| task | goal | reward_text | offer_dialog |
|---:|---|---|---|
| 100004 | `-` | `-` | 击败当前层怪物…（完整中文） |
| 100005 | `-` | `梦境徽章` | 战败我可以进入下一层! |

旧 `6/10` 固定拼 `goal + "\n" + reward_text`，把占位符原样送给
`SendTaskHallReq(0x01038CB2)` 的详情正文。NPC `26/1` 主对白用的是
`offer_dialog`，所以对话页正常、接取确认页异常。

## 根因

接取确认文案选错字段：自定义任务把可读说明放在 `offer_dialog`，`goal`/`reward_text`
可留 `-`；服务端未识别占位符，违反“详情页应展示可读任务说明”的契约。

## 修改

- `vm_net_mock_task_build_hall_info()`：跳过单独的 `-`；接取优先 `offer_dialog`，
  进行中优先 `active_dialog`，提交态优先 `completed_dialog`；真实 goal+reward 路径不变。
- `6/12` 目的地说明同样避开 goal 占位。
- NPC 选项 description 不再下发裸 `-`。

## 验证

- `make -j2`
- 点 `32901` →「接受挑战守关boss」：确认页应为「战败我可以进入下一层!」
- 普通 task.dsh 任务（有真实 goal/reward）确认页仍为 `goal\nreward`
