# 组队邀请名称编码取证（2026-08-10）

phase: team-invite-name-encoding
status: fixed-pending-runtime-verification

## 触发条件

一个在线角色向另一个在线角色发起组队邀请。接收端在下一次场景同步轮询中解析
`5/2 { id, name }`，邀请确认提示中的角色名显示乱码。

## 已确认链路

1. 发起端操作发送 `5/1 { id }`；
   `vm_net_mock_build_nearby_team_invite_response()` 解析目标并调用
   `vm_mock_service_session_enqueue_social_notice()`。
2. 入队列时，`sourceRole->name`（或已在线会话的 `onlineRoleName`）原样复制到
   `vm_mock_service_social_notice.sourceName`。
3. 接收端的正常场景轮询由
   `vm_net_mock_append_scene_sync_social_notice_object()` 取出通知，构建
   `5/2 { id: sourceWireId, name: sourceName }`。`name` 通过
   `vm_net_mock_put_object_string()` 以 `strlen` 的原始字节和 16 位长度写出，未进行
   转码。
4. `江湖OL.CBE:net_handle_group_info(0x01011F3A)` 的 subtype 2 读取 `id`、
   `name`，再用格式串 `"%s"` 把 `name` 直接传给原生确认提示；没有第二个名称字段。

当前角色名数据库列以 `HEX(role_name)` 读取，并逐项按 GBK 解码验证；现有数据均为
合法 GBK。因此 UTF-8/GBK 转换不是本问题的修复方向。

## 当前取证

`mock_server_social.c` 在构建 `5/2` 前写入：

```
team_invite_name_wire ... wt=5/2 field=name encoding=raw-gbk bytes=<n> hex=<hex>
```

一次真实复现的 `5/2.name` 是 `CEE4C1D6C3CBD6F7`，即“武林盟主”的 GBK 原始字节。
故名称来源和编码在对象字段之前均正确。客户端却在该名称后显示随机文字，且同意后的
`5/4` 提示更严重；这与 `net_handle_group_info` 对二者均以 `%s` 直接格式化完全一致。

此前 `vm_net_mock_put_object_string()` 只下发 GBK 正文，内层长度不包含 C 终止符。对一般
长度读取字段这是正确的，但 `5/2.name` 与 `5/4.name` 的客户端分支并不读取长度后再复制：
它们将 `GetString()` 返回地址直接交给 `%s`，因此越过字段尾部读取后续 WT 字节或未初始化
缓冲，造成名称后随机尾字。

## 修复

新增 `vm_net_mock_put_object_cstring()`，它保留 WT blob 与内层长度格式，只在有效数据末尾
加入一个 `NUL`。仅 `5/2.name` 和 `5/4.name` 使用该 helper；`5/3`、`5/5`、`5/10` 的
`groupinfo` 名称仍是序列长度字符串，不增加终止符，也不修改其解析游标。

`team_invite_name_wire` 与 `team_result_name_wire` 日志继续保留到本修复经过一次运行时复测；
它们只读取字节，不改变响应。

## 验证边界

下一次复现应确认：

1. 接收端 `5/2` 确认框只显示邀请方名称；
2. 发起端收到的 `5/4 result=1` 只显示接受方名称；
3. 紧接的 `5/5` 队伍增量仍能正确建立队员行。

此前的取证说明如下，供继续审计：

该日志只读取即将写入 `name` 的字节；不修改队列、响应、客户端状态或编码。
同意/拒绝结果 `5/4` 也会记录同一格式的
`team_result_name_wire ... wt=5/4 ... hex=<hex>`。客户端同样以 `%s` 直接消费该字段，
且正常同意时 `5/4` 后会在后续场景轮询紧接 `5/5` 成员增量，故必须单独核对它。
下一次复现应把该十六进制值与发起方角色名的 GBK 字节比较，定位首次偏离点：

- 字节不是正确 GBK：排查名称进入服务会话前的来源；
- 字节正确且出现尾随随机文字：核对是否使用了 C-string helper，不能用通用长度字符串
  builder 替代。

## 已排除的假设

- 不能把服务端终端中打印中文时的乱码当作协议证据；PowerShell 的当前代码页可能错误
  显示 GBK 字节。
- `5/2.name` 不是 UTF-8 文本字段；角色数据持久化仍使用 GBK 原始字节。它的特殊点是
  原生 handler 的 C 字符串消费方式，而不是字符集。
