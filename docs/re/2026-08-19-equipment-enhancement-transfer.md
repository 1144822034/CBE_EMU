# 背包装备“重铸”强化转移进度条停滞

Date: 2026-08-19

Status: implemented; client runtime validation pending

## 1. 当前卡点

- 可见现象：背包查看装备，点击“重铸”，选择目标装备并确认后，进度条一直存在。
- 触发方式：先进入装备的强化页面，再进入客户端称为“重铸”的装备选择流程。
- 本轮最小目标：实现固件原生 `1/29/5` 预览和 `1/29/6` 提交响应，让客户端通过正常 event 7 响应结束等待并完成强化等级转移。

## 2. 运行时证据

原始服务日志中的首次偏离是：

```text
mock_equipment_enhance phase=1 seq=268 item=12013 level=2 ... resp=29/1
net_send ... wt=29/1 ... source=builtin-equipment-enhance resp=287
[error][network] unhandled wt=29/5 len=31 objects=1 first=1/29/5:22
net_send ... wt=29/5 ... source=ignored-unhandled-server-only resp=0
```

客户端已经正常取得 `29/1` 强化页数据；确认目标装备后发出的单对象 `1/29/5` 没有服务端 handler，因此没有响应进入网络事件队列，固件设置的 pending 标志一直没有被 parser 清除。进度条是缺失响应的结果，不是独立 UI 层问题。

## 3. IDA 目标

| binary | function/address | findings |
| --- | --- | --- |
| `江湖OL.CBE` | `SendBattleSeqEvent(0x0101DAA0)` | 构造 `1/29/<subtype>`，依次写普通数值字段 `seqd`、`seqs`，然后设置网络等待标志。 |
| `江湖OL.CBE` | `SceneNodeCreateAndInit(0x0101DEDE)` | `0x0101E54C..0x0101E562` 取得目标/来源装备序号并发送 subtype 5；同一流程在 `0x0101E67A` 附近发送 subtype 6。 |
| `江湖OL.CBE` | `HandleItemUseAndEquip(0x01028C7C)` | `0x0102941A` 解析 `29/5`，`0x010295FC` 解析 `29/6`；两个分支入口都会先清除等待状态。 |

## 4. 调用链 / 业务流程

1. 客户端从背包装备详情进入强化页，既有 `29/1` 返回当前强化等级。
2. 客户端选择另一件装备；固件比较目标与原装备等级，通过后调用 `SendBattleSeqEvent(seqd, seqs, 5)`。
3. `29/5` 成功响应提供原装备等级、费用和对应等级玄晶。客户端建立 category 23 的材料/费用确认行。
4. 用户确认后固件用同一对序号发送 `1/29/6`。
5. 服务端重新校验状态，扣除铜钱和玄晶，把原装备强化等级转给目标装备，并持久化。
6. `29/6` 成功响应使客户端按序号更新玄晶数量以及两件装备的当前强化等级，然后显示固件内置“转移成功”。

## 5. 结构体 / 状态字段笔记

- 服务端权威实例：`vm_net_mock_role_state.backpackItems[]`。
- 装备实例字段：`seq`、`enhanceLevel`、`enhanceAffixes`。
- `seqd`：destination，目标装备序号。
- `seqs`：source，原装备序号。
- `29/5.level`：原装备当前强化等级；固件把它保存到强化转移控制器并用于提交后的费用显示/扣减。
- `29/5.flag`：对应玄晶可用状态。固件提交前检查材料行状态，值 `2` 进入缺少玄晶路径；当前服务端使用 `1=可用、2=缺少`。
- 不复制或交换 `enhanceAffixes`。响应协议只更新等级；目标装备保持自己的实例词条方案，并在新等级需要时补齐尚未生成的阶段词条。

## 6. 请求 / 响应契约

### Request `1/29/5` 与 `1/29/6`

- 单对象：`major=1, kind=29, subtype=5|6`。
- 字段：`seqd:number`、`seqs:number`。
- 两序号必须非零、在 `u16` 范围内且互不相同。
- 实际 `29/5` 包长 31 字节，object payload 22 字节，和两个字段完全吻合。

### Response `1/29/5`

- 先读 `result:u8`。
- `result=1` 时继续读 `flag:u8,id:i16,seq:u32,name:string,money:u32,level:u8`。
- `result=3` 时不读玄晶字段，但仍读 `money:u32,level:u8`；这是原装备等级为 1、无需对应玄晶的正常预览路径。
- `result=2` 显示固件内置“装备不存在”。
- 强化等级 `L>1` 对应 `item.dsh` 的玄晶 `900+L`（901..916）。

### Response `1/29/6`

- 先读 `result:u8`。
- 成功字段顺序：`seq:u16,num:u8,seqd:u16,curleveld:u8,seqs:u16,curlevels:u8`。
- `seq`、`seqd`、`seqs` 必须编码为 getter `+0x48` 对应的 tagged-u16，字段值字节为 `{00 02 <u16-be>}`；普通 tagged-u32 会被该 getter 读成 0。
- `seq/num` 是被消耗玄晶的背包序号和数量；等级 1 不消耗玄晶，但 parser 仍无条件读取，所以发送 `0/0`。
- 成功值：`curleveld=原装备提交前等级`，`curlevels=0`。
- 错误码：`2=装备不存在`、`3=没有对应玄晶`、`4=金钱不足`、`5=不可转移`，其他值显示“转移失败”。

## 7. 成功路径与失败路径

### Success path

- 预览与提交都严格按 `seqd/seqs` 查找两件背包装备。
- 提交阶段重新校验来源等级大于 0、目标等级小于来源等级。
- 等级大于 1 时消耗一个对应等级玄晶；扣除铜钱；目标取得来源等级，来源归零；一次持久化成功后返回 `result=1`。
- 持久化失败恢复完整角色快照，并返回通用失败，不把半完成状态暴露给客户端。

### Failure path

- 当前实际失败：请求无人处理，响应长度为 0，客户端 parser 从未运行，pending 状态无法结束。
- 提交期业务失败使用固件已有的 2..5 错误码，不改变装备、玄晶或铜钱。

## 8. Negative Evidence

- 不是 `29/1` 强化页 parser 错误：同一日志已显示 `builtin-equipment-enhance resp=287`。
- 不是响应字段缺少或错序：当前根本没有 `29/5` 响应。
- 不能用空 ACK 或宿主点击关闭进度条：`29/5` parser 需要材料、费用和等级来建立下一阶段，`29/6` parser 需要三个实例更新结果来保持客户端状态一致。
- 固件字符串为“转移成功 / 没有对应玄晶 / 不可转移”，证明 UI 的“重铸”在该路径并非随机刷新词条，而是强化等级转移。

最新复现仍显示 `29/5` 首次偏离在 detector/parser：

```text
[error][network] unhandled wt=29/5 len=31 objects=1 first=1/29/5:22
net_send ... source=ignored-unhandled-server-only resp=0
```

为避免猜测字段编码，服务端现在只在未处理的 `29/5` 上输出不超过 256 字节的原始十六进制；该日志是只读取证，不改变响应或客户端状态。

## 9. Unknowns / Hypotheses

- 原服务端铜钱费用公式没有出现在固件中；客户端只消费服务端下发的 `money`。
- 当前服务端策略定为 `sourceLevel * 100`，与现有强化每级费用尺度一致。这是明确记录的服务端平衡策略，不声称是原服精确公式。
- `29/5` 没有已知的“不可转移”预览错误码；正常客户端在发送前已经比较等级。服务端只对装备不存在返回已证实的 `result=2`，提交期再用 `result=5` 权威复核等级关系。
- `SendBattleSeqEvent(0x0101DAA0)` 的 IDA 反编译确认字段写入参数是整数，但不同对象 writer 可能把序号编码为普通 `u32` 或外层长度为 4、内层 `{0,2,u16}` 的标记值；两者包长均为 31 字节。解析器仅在严格的 `seqd/seqs`、单对象、非零 `u16` 范围内兼容这两种表示，真实客户端十六进制仍需复现日志最终确认。

## 10. 本轮实现计划

- `src/server/mock_server_catalog.c`：新增严格的 transfer request 结构和 parser。
- `src/server/mock_server_equipment_npc.c`：新增预览/提交 builder、材料与费用校验、快照回滚。
- `src/server/mock_server_dispatch.c`：在现有强化 handler 相邻位置加入稳定 source `builtin-equipment-transfer`。
- `scripts/equipment-enhancement-transfer-regression.c`：覆盖 detector、预览字段、拒绝码和纯内存转移规则。

## 11. 验证清单

- [x] `29/5` 和 `29/6` 仅由精确 detector 命中。
- [x] `29/5` 等级 1 返回 `result=3,money,level`。
- [x] `29/5` 等级大于 1 按 parser 顺序返回玄晶和费用字段。
- [x] 缺玄晶、缺钱、非法等级关系分别走固件已有错误路径。
- [x] 成功后目标取得等级、来源归零、玄晶减 1、铜钱扣除。
- [x] 等级 1 成功包仍发送 `seq=0,num=0`。
- [x] 保存失败恢复完整角色快照。
- [x] 成功 `29/6` 严格只有一个对象，三个实例序号均为 tagged-u16。
- [x] `make -j2` 通过。
- [ ] 客户端由正常 event 7 响应关闭等待状态并进入确认/完成界面。
- [ ] 收集并核对 `unhandled_29_5_hex`，确认实际请求编码后完成客户端复测。

## 12. 实现与自动化结果

新增稳定 packet source：

```text
builtin-equipment-transfer
```

实现位置：

- `mock_server_catalog.c`：严格解析普通 `u32` writer 和外层长度 4、内层 `{0,2,u16}` writer 的两个字段，要求单对象 `1/29/5|6`、只含 `seqd/seqs`、两值非零且不同。
- `mock_server_equipment_npc.c`：预览、内存事务、持久化事务和纯响应编码四层分离；生产 builder 固定使用 `vm_net_mock_role_db_save("equipment-transfer")`。
- `mock_server_dispatch.c`：在 `builtin-equipment-enhance` 相邻位置分发，未扩大其他 `29/*` 请求的处理范围。

确定性回归命令：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 `
  -ffunction-sections -fdata-sections -w `
  scripts/equipment-enhancement-transfer-regression.c `
  obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o `
  obj/server/md5.o '-Wl,--gc-sections' `
  -o tmp/equipment-enhancement-transfer-regression.exe `
  -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\equipment-enhancement-transfer-regression.exe
```

结果：

```text
equipment enhancement transfer regression passed: exact 29/5+29/6 parser, preview, rejection, commit, tagged-u16 instance updates, and persistence rollback
```

该回归不启动监听器、不连接 MySQL，也不修改账号；它使用隔离角色和有界目录夹具，并通过显式成功/失败保存回调验证提交一次性和完整快照回滚。最终 `make -j2` 同样通过。

自动化已证明请求签名、响应对象、字段顺序、业务状态和持久化失败边界；尚未启动或操控用户客户端。客户端人工复测时应确认：`29/5` 日志 source 为 `builtin-equipment-transfer`，响应非零并作为普通 event 7 投递，预览界面显示费用/玄晶，确认后出现固件“转移成功”，两件装备等级、玄晶数量和铜钱在重新登录后仍一致。

## 13. 2026-08-19：转移成功后背包显示未刷新

最新运行日志确认 `29/6` 已成功提交并持久化：目标 `seqd=275` 从 `0` 变为 `1`，来源 `seqs=281` 从 `1` 变为 `0`，随后投递了 `response=29/6+17/1`；但用户复测确认背包仍显示转移前等级。这排除了服务端状态、持久化和响应分发失败，也直接否定了“追加 `17/1` 即可重建当前背包显示”的上一版假设。

第一次偏离发生在原生 `29/6` 对象的字段解码。IDA 中 `江湖OL.CBE:HandleItemUseAndEquip(0x01028C7C)` 的成功分支分别在 `0x0102962C`、`0x01029720`、`0x0102974E` 通过对象 vtable `+0x48` 读取 `seq`、`seqd`、`seqs`，而 `result/num/curleveld/curlevels` 通过 `+0x4C` 读取。项目已有同一对象实现证据表明 getter `+0x48` 要求 tagged-u16；当前响应却把三个序号写成 tagged-u32，因此客户端取得序号 0，后续 `UpdateTaskProgressEntry(0x01028726)` 无法找到玄晶及两件装备实例。成功提示仍能出现，是因为 `result` 使用了正确的 `+0x4C` 编码。

根因修复位于 `29/6` 响应契约所有者：`seq/seqd/seqs` 改为 `{00 02 <u16-be>}`，并删除当前业务回调不会消费的附加 `17/1`，恢复单对象原生响应。`curleveld/curlevels` 继续保持 tagged-u8。定向回归锁定对象数为 1、字段顺序和三个序号的精确类型，防止以后再次用数值相同但 getter 不兼容的 u32 编码。
