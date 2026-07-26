# 下行 GBK 中文审计

## 范围与方法

本次审计的对象是 CBE 客户端会直接消费的服务端文本字段，而不是后台网页的 UTF-8
错误提示或仅用于日志的中文注释。客户端包字符串仍按 GBK 处理；资源文件中的名称、
任务、NPC、角色与聊天文本也都以 GBK 二进制数据保存。

审计包括：

1. 扫描 `src/server/*.c` 所有含 `\x` 的 C 字面量，按 C 的完整十六进制转义规则
   重建字节，再用 GBK 解码；共检查 267 个候选，没有非法 GBK 字节，也没有
   `\xA3\xBA1` 一类“数字被转义吞并”的溢出。
2. 枚举 `vm_net_mock_put_object_string`、`vm_net_mock_put_object_blob` 及其调用的
   客户端可见字段，追溯固定文本、DSH/SCE/XSE 资源文本、MySQL `VARBINARY` 文本和
   后台 UTF-8 入库转换路径。
3. 通过隔离 mock-service 发出真实 WT 请求并解码响应中的嵌套字符串。

## 发现的首个偏离

有两条死亡提示直接把 UTF-8 源码中文交给了 GBK 字段：

- `4/1` 碰怪/挑战时角色 HP 为 0 所附加的 `25/11 {result=8,info}`；
- 死亡选择失败后的 `20/1 {result=1,info}`。

`vm_net_mock_put_object_string` 只包装长度，不做字符集转换，客户端则把字段当作
GBK。因此 UTF-8 多字节会被错误渲染；这就是协议层首次偏离，而不是文本控件问题。

## 修改

- `mock_server_battle.c`：两条 `25/11.info` 固定死亡提示改为显式 GBK 字节，并在
  构造函数旁明确该字段不能接收 UTF-8。
- `mock_server_interaction_login.c`：所有 `20/1.info` 的复活失败文案与默认值改为
  显式 GBK。
- 之前修复的修炼天书实例 `7/38.bookdes` 和 `7/40.bookinfo` 继续使用已持久化的
  GBK 字节；不再存在尾随数字被 `\x` 转义吞入的写法。

已确认不需要修改的路径：DSH/SCE/XSE 文本与角色、好友、帮派、聊天、动态 NPC、
任务字段均在服务端以 GBK 原字节/`VARBINARY` 流转；后台动态 NPC 和任务编辑入口
在 UTF-8 表单入库前显式调用 `utf8_to_gbk`。后台 HTTP 错误文案保持 UTF-8，不会
下发到 CBE。

ASCII 提示（例如 `Monster not ready`）在 GBK 中也是有效 ASCII，本次没有把语言
本地化需求误当作编码修复。

## 验证

- `make -j2` 通过。
- 隔离服务 `127.0.0.1:19094` 使用 HP=0 测试角色，登录并发送 `1/4/1` 后，
  `25/11.info` 原始字节
  `C4FAD2D1BEADCBC0CDF6A3ACC7EBCFC8CAB9D3C3B8B4BBEECAAF` 解码为
  “您已经死亡，请先使用复活石”。
- 随后发送 `1/7/14 {result=1}`，`20/1.info` 解码为“复活石不可用”。
- 测试夹具已删除，不影响本地账号数据。
