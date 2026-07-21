# 副本 NPC 挑战确认进度层

## 现象

动态副本 NPC 的“挑战守关怪”请求为 `1/26/1 {type=2,id=ECxxxxxx}`。旧实现只返回 `1/30/9 {isleader,challenge}`。客户端能画出“确定/取消”按钮，但上一请求的取数进度条仍处于最上层并拦截输入，因此没有后续 `1/30/10`，服务端的待确认挑战一直无法推进。

## 运行时证据

- 服务端依次收到 NPC 对话、实例菜单和挑战选项请求，最后记录 `mock_npc_instance_challenge_prompt ... response=30/9`；此后没有客户端请求。
- 当前构建自动化复现中，客户端依次收到 `resp=103`、`resp=101`、`resp=57`。收到 57 字节 `30/9` 后，画面出现确认/取消按钮，但中央取数进度条持续存在，确认键和左软键都无法触发网络请求。
- 复现截图为 `bin/autotest/screens/000026_00052231.bmp` 至 `000033_00066279.bmp`；它们仅用于运行时验证，协议结论以下述 IDA 解析器为准。

## 客户端解析证据

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `task_hall_activate_selected_entry(0x010492B0)` | action 1 发送 `26/1 {type=2,id}`，随后执行任务大厅清理，但网络 pending 状态仍等待 kind-26 响应。 |
| `江湖OL.CBE` | `DispatchItemEvent(0x01039C28)` | kind 26 的任意 subtype 在分支结束时清 `r9+21808` 和 `r9+21804`；未知 subtype 不进入业务 UI 解析，适合作为无副作用确认。 |
| `江湖OL.CBE` | `HandleChallengeResponse(0x010395AA)` | `30/9` 读取 `isleader` 与 `challenge`；`isleader=0` 时注册确认/取消回调，但不清 kind-26 pending 字段。 |
| `江湖OL.CBE` | `HandleResConfirmCb(0x01039566)` | 确认回调调用 `SendGameEvent(0,0,30,10)`。 |
| `江湖OL.CBE` | `SendGameEvent(0x01037E56)` | group 30/subtype 10 分支读取客户端确认状态并写入 signed-byte `agree`，所以真实请求为 20 字节 `30/10 {agree}`。 |

## 修正契约

挑战选项响应改为同一 WT 包中的两个有序对象：

```text
1/26/0 {}
1/30/9 { isleader, challenge }
```

客户端先通过 `26/0` 清除原请求进度状态，再由 `30/9` 安装确认框。`26/0` 不匹配 kind-26 的任何业务 subtype，因此不会创建额外菜单。确认后严格走 `30/10 {agree} -> 4/10 {side,battleinfo}`；取消按钮不发送 `30/10`。服务端只接受单对象、20 字节、包含一个合法 `agree` 字段且当前连接仍持有未过期挑战记录的请求，没有直接启动战斗，也没有修改客户端状态。
