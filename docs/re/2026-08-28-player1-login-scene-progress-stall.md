# player-1 登录进入场景后进度条未收尾

## 现象与首次偏离

角色 `10871` 完成选角和首登装备分阶段同步后，客户端进入
`01桃花岛_02.sce`。服务端记录的首个偏离是 CBE
`scene_runtime_init_and_sync()` 发送的裸 `WT 12/1`（19 字节）：

```
1/12/1:0, 1/7/42:0, 1/25/5:0
```

该请求是场景进度条的收尾契约。它应收到有序的
`1/12/1 + 1/7/42 + 1/17/1 + 1/25/5`，但原始运行记录为
`source=ignored-unhandled-server-only resp=0`；后续场景 NPC 和任务请求仍可出现，说明卡住的是
这一个未完成的 CBE 场景初始化窗口，而不是场景名或落点数据。

## 已排除的假设

- 这不是 61 格背包或完整强化词条导致的服务端响应容量溢出。实际角色刚生成成功了
  `30/21` 的 61 行、3051 字节全属性背包快照；同一行集合的 `17/1` 不携带 seq/count，必然更小。
- 扩展 `first-login-equipment-attribute-bootstrap-regression` 后，夹具会在真实
  `7/7(type=2)`、`7/7(type=3)` 完成后填满 61 个具有四档强化词条的客户机格子，再经真实
  dispatcher 提交上述 19 字节请求。它稳定返回四个对象，`17/1.iteminfo=4160` 字节、总响应
  4282 字节。
- 服务端总响应缓冲为 131072 字节；本次最坏形状远低于该边界。

因此不能以裁剪背包、丢弃强化词条、修改 CBE 或在宿主重放/重排网络事件的方式处理。以上均会
掩盖真正的响应构造失败，且不符合平台边界。

## 根因与修复

第二次运行将失败精确定位为：

```text
mock_training_book_list_failed stage=booksinfo role=10871 pos=152 \
  out_cap=131072 books=0 bytes=0
```

角色没有 921 训练书，因此 `1/7/42.booksinfo` 的正确值是零字节字段。服务端的聚合构建版本
`vm_net_mock_put_bytes()` 一直允许这个合法表示，但生产服务实际链接的
`src/server/mock_server_packet_fields.c` 额外拒绝了 `data == NULL`，没有先判断长度。于是
`vm_net_mock_put_object_raw(..., NULL, 0)` 被错误地判为失败，整个 19 字节场景收尾请求又落回
unhandled 路径。

修复统一了两种构建方式的字段边界：长度为零时允许空数据指针且不复制任何字节；长度非零时
仍严格要求有效指针。它不创建训练书数据、不改变角色、不会裁剪背包，也不干预 CBE 回调或场景
状态。修复后该空对象的正常 wire 形式是：

```text
1/7/42 { booknum=0, booksinfo=<zero-byte raw field> }
```

## 验证

已执行：

```powershell
make -j2
make packet-fields-zero-length-regression
.\obj\server\packet-fields-zero-length-regression.exe
make first-login-equipment-attribute-bootstrap-regression
.\obj\server\first-login-equipment-attribute-bootstrap-regression.exe
```

两项隔离回归均通过：前者直接编译并执行生产拆分字段写入器，断言零长度空指针成功、非零长度
空指针仍拒绝；后者覆盖真实 `7/7(type=2/type=3)` 后带 61 个完整词条背包的 19 字节场景收尾。
它们不监听端口、不连接 MySQL、不读取或写入 player-1 数据。

构建已经生成修复后的 `bin/jh-online-server.exe`；运行中的用户服务不会被构建替换或重启。
