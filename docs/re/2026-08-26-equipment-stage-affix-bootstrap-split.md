# 强化阶段词条的首次背包实例化与安全分包

Date: 2026-08-26

Status: temporary legacy compatibility fallback active.  The server has restored compact `30/21` rows for all clients; the newer client-side split remains in source, pending a proved per-connection capability negotiation.

## 当前临时兼容策略

2026-08-26，旧版 Android APK 登录后出现“解包错误”。原因是该 APK 不含客户端
分包逻辑，却收到包含完整四档强化词条的首登 `30/21` 组合包。为恢复旧 APK 登录，
服务端目前对所有客户端重新发送紧凑的 `30/21` 行：仅包含当前/最高强化和
`attrCount=0`。

这不会删除或修改数据库中的装备、强化等级或词条；已穿戴装备仍通过 `7/7 type=2`
的完整 common-extra 行下发四档词条。代价是未穿戴的背包装备会重新回到旧行为：首次
实例化后不显示灰色 `+4/+8/+12/+16` 词条，且仅凭后续 `17/1` 或 `29/3` 不能补回。

`src/network-client.c` 的原子分包代码没有回退：旧 APK 从未包含它，回退不会消除旧
APK 的解包错误；保留它也不会改变服务端当前紧凑行的字节。完整行恢复需先实现并验证
新版客户端显式能力声明，再仅向已声明的连接发送完整 `30/21`。

## 触发与首个偏离

部分背包装备的详情页没有灰色的 `+4/+8/+12/+16` 强化阶段词条；将它强化至 `+4`
后，强化等级会更新，但第一条阶段词条仍不出现。

服务端持久化链路并不是首次偏离：装备实例在加载和强化成功时都会调用
`vm_net_mock_equipment_enhancement_ensure_affixes()`，四档类型/数值由
`enhance_affix_types`、`enhance_affix_values` 落入背包或已穿戴实例；保存失败会把
整个角色快照回滚并返回 `29/3 result=0`。

首个偏离在登录/重进的 `1/30/21` 背包网格：为避免大背包的合并响应耗尽客户端下行
解析池，该行被编码为 `attrCount=0` 的紧凑形式。`HandleItemGridResponse`
(`JianghuOL.CBE:0x01039952`) 正是在此创建主背包实例；后续 `1/17/1` 只更新背包
列表缓存，不能回写该实例的词条数组。`1/29/3` 的
`UpdateTaskProgressEntry(0x01028726)` 也只写当前/最高强化等级，不能创建阶段数组。

所以“初始无灰字、+4 后仍无词条”是首次实例化缺少四档计划的结果，而不是词条刚在
数据库中消失。

## 不能直接恢复旧单包

历史运行时样本中，48 个网格行包含 31 件装备时，完整 `30/21.iteminfo` 为 2950
字节；它与组同步、储量、装备同步和状态对象合成一个 `5/10 + 7/7(type=1)` 回复。
客户端在业务分发前执行 `event_packet_init(packet, 10, 19)`：固定开销为
`10 * 88` 的对象表、每对象 `19 * 12` 的字段表，再加被复制的字段内容。

原始复合包的解析分配为：

```text
10 * 88 + 7 * (19 * 12) + (3892 - 5 - 7 * 6) - 2 * 19 = 6283 bytes
```

该包语法、对象数和字段数均有效，但固定解析池耗尽，客户端会在任何业务对象之前显示
“解包错误”。因此不能仅把完整行恢复到原来的同一回复；也不能用 `17/1`、重复 `30/21`
或伪造 `7/7 type=2` 去覆盖已经存在的装备实例。

## 已暂停的完整响应契约

此前服务端曾恢复 `30/21` 装备行的完整 common-extra：当前/最高强化、属性数和稳定的
四条 `+4/+8/+12/+16` 实例词条。该编码当前已暂停，等待能力协商后再按连接启用。

客户端远程传输层只对下列**精确的首登组同步形状**拆分正常 event-7 事件：

```text
原始服务端回复：
  group state + 10/26 + 30/21 + [7/11] + [7/7 type=2, 7/7 type=3] + 7/20 + 7/32

第一个 event-7：
  group state + 10/26 + 7/20 + 7/32

第二个 event-7：
  30/21 + [7/11] + [7/7 type=2, 7/7 type=3]
```

匹配要求唯一 `30/21`、存在 `5/10` 和 `10/26`，可选储量行必须在网格之后；装备
`type=2` 与其零行 `type=3` 完成通知必须成对且相邻。任何不满足该形状的包保持原样。
两帧仍通过既有 scheduler 的普通数据事件投递，不写 CBE/CBM 内存、不改寄存器、不直接
调用客户端业务回调。

这保留了关键的客户端顺序：网格先创建背包实例，之后储量行与穿戴装备同步继续按原始
顺序处理；`type=3` 仍在 `type=2` 后触发既有的装备属性重算完成分支。

## 后续风险修复：分帧必须原子交付

最初的分帧实现正确拆出两个 WT 包，但在宿主传输层先单独分配并入队首帧，再独立处理
第二帧。服务端构造组同步时已经设置同角色的背包引导标记；若第二帧在客户机内存分配失败，
或 scheduler 的 8 个网络任务仅剩一个可用槽位，客户端可能只收到首帧，服务端又不会在
后续普通组同步中重发 `30/21 + 7/7(type=2,type=3)`。这不会删除 MySQL 中的装备实例，
却会让本次登录的背包与装备栏看似没有加载。

修复位于宿主传输层，仍只投递原始服务端 WT 对象：

- 仅上述精确首登分帧形状标记为“必须成对交付”。
- worker 将两个宿主帧复制进同一块宿主内存；进入客户机时也只进行一次连续的
  `vm_malloc`，从该块的两个偏移分别作为两个 normal event-7 的数据指针。
- 入队前必须有两个空闲网络任务槽；同一 emulator 线程中随后成对入队，因而不会接受
  半个登录结果。
- 暂无两个槽或客户机缓冲区时，保留原始两帧在 completion 队首并在后续 tick 重试，阻止
  后到响应越过它。日志分别记录 `remote_login_backpack_bootstrap_retry` 与
  `remote_login_backpack_bootstrap_queue`，用于区分资源等待和成功交付。

该修复不触碰角色的装备 ID、槽位、词条、耐久或数据库保存事务。

## 修改点

- `src/server/mock_server_catalog.c`
  - `vm_net_mock_build_backpack_grid_iteminfo_blob()` 当前再次使用
    `vm_net_mock_seq_put_item_compact_extra()`，以兼容不具备分包能力的旧 Android。
  - `7/7 type=2` 已穿戴装备路径仍使用完整 common-extra；场景启动的紧凑 `17/1`
    路径也保持不变。
- `src/network-client.c`
  - 新增窄匹配的 `vm_client_extract_login_backpack_bootstrap_followup()`；它只重组已由
    服务端生成的 WT 对象，并通过现有 event-7 follow-up 队列按顺序投递。
- `scripts/first-login-equipment-attribute-bootstrap-regression.c`
  - 首登夹具新增一件 `+0` 背包装备，断言 `30/21` 行有四条门槛为
    `4/8/12/16` 的词条，而普通物品仍为零属性行。
- `scripts/equipment-enhancement-bootstrap-split-regression.c`
  - 构造含 2950 字节完整网格的历史大包形状；断言原始开销超过已记录的 6283 字节失败量级，
    分包后两个 event-7 包均低于该量级，且对象顺序严格保持。

## 验证

1. `make -j2` 已重新编译客户端与服务端源对象；正式
   `bin/jh-online-server.exe` 正在运行并锁定输出文件，最终覆盖链接被系统拒绝。
   未停止或替换用户正在运行的服务。
2. 使用同一批新服务端对象链接到独立临时文件，链接成功。
3. 隔离服务端夹具（不连接 MySQL、不启动监听器）通过，日志确认：

```text
mock_backpack_grid ... gridnum=2 ... iteminfo_len=54
mock_equipment_login ... rows=2 iteminfo_len=161 response=7/7-type2
first-login legacy compact bootstrap regression passed type3_completion=1
```

临时兼容回退后，该夹具应输出 54 字节：一条普通 27 字节行加一条紧凑装备行；
`7/7 type=2` 的已穿戴装备行未改。

4. 隔离客户端传输夹具（不启动模拟器、不连接服务）通过：

```text
remote_login_backpack_bootstrap_split original=3912 primary=234 followup=3683
equipment enhancement bootstrap split regression passed
```

5. 后续原子交付夹具（同样不启动模拟器、不连接服务）通过：

```text
remote_login_backpack_bootstrap_retry seq=77 attempt=1 reason=net-queue ... action=retain-both
equipment enhancement bootstrap atomic delivery regression passed
```

它把 scheduler 填至仅剩一个槽，断言不会放入首帧；随后验证两槽可用时两帧按顺序一起
入队。`make -j2` 也在本次修复后通过。

还需要在重启并同时使用更新后的 `main.exe` 与 `jh-online-server.exe` 后，按正常路径验证：
登录已有装备角色，查看背包装备的灰色四档词条，再从 `+3` 成功强化到 `+4`。预期第一条
词条无需重登或额外刷新即按既有阶段阈值解锁，同时不得出现“解包错误”。
