# 任务提交后的详情刷新组合包

## 运行时现象

动态 NPC 使用 `00开门红包.xse` 时，客户端接取任务 `3001`，再次与 NPC
交谈并提交后，服务端已经完成任务并发放 `888` 钱币奖励。紧接着客户端发送：

```text
WT len=51
1/25/5 payload=0
1/6/10 payload=37
```

旧任务处理器仅接受 `25/5 + 6/4` 提交组合，因此该后续请求落入
`unhandled`，客户端等待提示没有收到收尾对象而一直显示。

## 客户端证据

- `task_hall_request_task_detail(0x010491FA)` 调用
  `SendGameEvent(state, taskid, 6, 10)`。
- `SendGameEvent(0x01037C02)` 对 `6/10` 写入 `taskid`、`state`，随后在第二个
  subtype 10 分支追加 `agree`，三字段恰好组成 `37` 字节 payload。
- `SendTaskHallReq(0x01038CB2)` 从 `6/10` 响应读取普通字符串字段 `info`，
  复制详情并结束任务大厅等待状态。
- `net_handle_info_banner_state(0x01010C7E)` 处理信息/等待状态；既有真实链已验证
  `25/5 {result=4}` 是对应空 `25/5` 请求的完成应答。

IDA 实例按 `binary_name=江湖OL.CBE` 动态选择，没有固化 instance id。

## 服务端契约

`vm_net_mock_build_task_response` 只新增一个精确例外：

```text
request:  25/5(empty) + 6/10{taskid,state,agree}
response: 25/5{result=4} + 6/10{info}
```

响应对象顺序与请求保持一致。其他以 `25/5` 开头的场景包仍不进入任务详情
分支，原有 `25/5 + 6/4` 提交和 `6/11 + 25/5` 接取契约不变。

## 回归

`tmp/task-detail-combo-regression.php` 构造实测长度为 `51` 的组合请求，并校验：

- 响应恰好有两个对象；
- 第一个为 `25/5` 且带 `result`；
- 第二个为 `6/10` 且带 `info`；
- 请求不修改任务或角色持久化状态。
