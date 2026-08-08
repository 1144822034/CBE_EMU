# 装备回收商人出售后的进度条停滞（2026-08-08）

## 触发

1. 在场景中与服务类型为“装备回收”的动态 NPC 对话。
2. 打开出售装备列表并选择一件背包装备。
3. 服务端成功写入铜钱和背包删除，但客户端的“获取数据”进度条不消失。

运行时原始服务日志已经固定为：

```text
mock_npc_service action=equipment-sell opcode=ee00004b value=75 ... result=1 options=6 money=50684 objects=3 resp=456
```

所以首次偏离不在请求识别、装备序号、价格计算或事务提交；它发生在成功响应的
第二个对象。

## 客户端契约与根因

请求是 `1/26/1 {type=2,id=0xee000000|backpack_seq}`。客户端
`task_hall_activate_selected_entry(0x010492B0)` 发出该请求，
`DispatchItemEvent(0x01039C28)` 对任意 `kind=26` 响应会在处理后清除
`R9+0x5520/+0x551c` 的等待状态。因此该请求的完成对象是
`1/26/1 {hidebtn,dialog}`。

旧响应在 `26/1` 后错误追加：

```text
1/7/7  {type=2,iteminfo=<sold equipment row>}
1/7/11 {info=<seq,0>}
```

按二进制名选择的 IDA 实例 `江湖OL.CBE` 与 `mmGameMstarWqvga.cbm` 证明：

- `mmGameMstarWqvga.cbm:sub_11CE(0x11CE)` 将 `1/7/7` 转给
  `sub_D04(0x0D04)`；
- `sub_D04` 对 `type=2` 构造装备数据，并调用物品管理器 vtable `+104`，参数为
  该装备记录和 `-1`；这是装备安装/同步路径，不是背包删除路径；
- `JianghuOL.CBE:HandleItemOperationResponse(0x01033544)` 的 `7/11` 分支仅按
  `seq` 改写已有行的计数字段。普通装备归零时它不会释放背包行。

故旧协议把一件已售装备重新交给装备安装路径，再以不具备删除能力的 `7/11` 归零。
这是对 `7/7 type=2` 语义的错误复用，也是进入错误后续 UI 状态的第一处契约违反；
不能通过吞掉进度条或伪造成功解决。

## 修复

出售事务仍在 `vm_net_mock_role_db_save("npc-equipment-sell")` 中原子提交，但成功响应
只保留 `1/26/1` 对话刷新。新菜单从已提交角色状态生成，已出售装备立刻不再出现在
回收选项中；当玩家打开背包时，客户端发起它原生的背包列表请求并从同一提交状态重建。

不再在 `26/1` 的对话 callback 中发送 `7/7 type=2`、`7/11` 或 `17/1`：前两者是错误
的装备/计数契约，后者仅由背包模块自己的 callback 消费。

## 回归

`php scripts/dynamic-npc-equipment-buyer-regression.php` 的隔离夹具验证：

- 6 个具有不同序号的同 ID 装备分页为 5+1；
- 出售 `seq=41` 后仅删除该数据库行、精确增加展示的铜钱，响应仅含 `26/1`；
- 再次提交同一序号不再加钱，且不下发物品变更对象。

仍需由真实客户端验收：出售后进度条消失、回收菜单立即移除该装备、关闭并打开背包后
该装备不再出现。
