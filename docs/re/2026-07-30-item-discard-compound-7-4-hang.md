# 连续丢弃卡死：同 WT 双 7/4 被拒回空包

日期：2026-07-30

## 触发与现象

连续丢弃装备若干次后界面卡住。日志末尾：

```text
mock_item_discard ... seq=35 ... source=builtin-item-discard resp=282
...
unhandled wt=7/4 len=72 objects=1 first=1/7/4:29,1/7/4:29
account=... request=72 response=0 source=ignored-unhandled-server-only
```

前序成功丢弃均为 `WT 7/4 len=38` / `first=1/7/4:29`。

## 链路

1. 客户端发出物品丢弃 `7/4`（通常单对象、payload=29、总长 38）。
2. 服务端 `vm_net_mock_parse_item_discard_request` 读第一个 `1/7/4` 后要求
   `offset == requestLen`。
3. 本次请求总长 72：header `objectCount=1`，但体里连续两个 `1/7/4:29`
   （4 + 34 + 34 = 72）。
4. 解析失败 → `build_item_discard_response` 返回 0 → CBMR 空体被静默忽略。
5. `JianghuOL.CBE:0x01033544` 只在收到 subtype 4 时清物品操作 waiting flag。

首次偏离：服务端拒绝本可识别的首个 `7/4` 选择器，回空包；客户端永久等待。

## 根因

严格单对象契约与连续丢弃时出现的同包双 `7/4`（objectCount 仍为 1）不兼容。
双对象更像同一次确认被写入两次 / 同 WT 合并，不是新的业务 subtype。

与既有卡死族同类（见 `2026-07-27-item-discard-freeze-and-bind.md`）：
缺 `7/4` 完成包 → wait 不清除。本次不是 MySQL 同步拖死，而是 parse 直接未识别。

## 修改

1. `parse_item_discard_request`：接受首个 `1/7/4` 后的额外 `1/7/4`，以及可选
   `1/2/10`；**未知尾随或无法继续解码的尾巴只跳过告警，不再 `return false`**。
   权威消费仍按首个选择器执行，`17/1` 负责列表对齐。
2. 若窄解析仍失败：只要包内能识别 `1/7/4`（`contains_object` 或 WT 头
   `7/4`），回 `7/4{result=2}`，禁止 `response=0`。

## 验证

1. `make -j2`，**重启** mock-service（旧进程仍会 `response=0`）。
2. 连续丢 10+ 件带补偿装备：不应再出现
   `unhandled wt=7/4` / `response=0`；若再现同包双对象，应见
   `mock_item_discard_compound` + `builtin-item-discard`。
3. 单对象 `len=38` 路径行为不变。

## 仍未知

客户端为何在连续丢弃若干次后把两个 `7/4` 写入同一 WT 且 `objectCount=1`
尚未用 IDA 发请求点钉死；当前按传输 companion / 双写兼容处理。
