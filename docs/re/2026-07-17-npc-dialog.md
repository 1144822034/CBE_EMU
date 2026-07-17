# 江湖 OL NPC 对话协议证据

## 目标与边界

本轮实现服务端驱动的 NPC 初始对白：角色在场景中选中 NPC 后，由客户端原生请求触发原生对话页。实现不修改 CBE 代码或客户端全局变量，也不从 `tmp/all_xse_export`、`tmp/all_sce_export` 等临时导出目录读取运行时数据。

当前范围是纯文本初始对白。任务选项、商店入口、提交物品和多轮分支仍需分别恢复客户端后续请求契约后再接入，不能把它们猜成普通文本按钮。

## 上行请求

`JianghuOL.CBE:SendNPCInteractReq(0x01037ED4)` 创建 NPC 交互请求，并依次写入：

- `id`：场景节点的 actor id；
- `index`：actor 在 25 个场景节点中的索引；
- `posx=0`；
- `posy=0`。

真实运行捕获为单对象 `1/26/1`，WT 包长 30，字段负载为：

```text
04 74 79 70 65 00 03 00 01 01
02 69 64 00 06 00 04 00 00 4E 7A
```

即 `type=1, id=20090`。虽然发送函数调用了 `index/posx/posy` 的写入方法，但 WT 序列化器会省略零值字段；因此索引为 0 的这次请求没有这三个字段。服务端检测器以“单对象 `26/1 + type=1 + 非零 id`”为必要条件，`index/posx/posy` 仅在存在时读取。

旧的通用 `type` 分支会把该请求误答成 `26/26`，日志表现为：

```text
net_send connect=0 wt=26/1 len=30 source=builtin-game-type resp=73
```

客户端分派器没有把这个响应当作 NPC 初始对白，所以界面不会出现。

## 下行解析

`JianghuOL.CBE:0x01039C28` 的网络分派逻辑在 kind 26、subtype 1 时调用 `ParseNPCDialogData(0x010380E8)`。解析器读取对象字段：

- `hidebtn:u8`；
- `dialog:raw`。

`dialog` 原始序列布局为：

```text
dialog-kind:u8
main-text:string
option-count:u8
repeat option-count:
  option-type:u8
  name:string
  flag:u8
  value:u32
  description:string
button-count:u8
repeat button-count:
  button-type:u8
  label:string
  final-byte:u8
```

本轮返回最小安全页面：`dialog-kind=0`、中文正文、`option-count=0`、`button-count=0`。对象仍为 `1/26/1`，并置 `hidebtn=0`。

## 服务端实现

`src/mock-server.c` 新增：

- `vm_net_mock_is_npc_dialog_request`：严格检测单对象 NPC 初始交互请求，并兼容 WT 零值字段省略；
- `vm_net_mock_npc_dialog_text`：为当前动态 NPC 提供 GBK 中文正文，未知 NPC 使用通用正文；
- `vm_net_mock_build_npc_dialog_response`：按 `ParseNPCDialogData` 的真实序列布局构造 `26/1` 响应；
- `builtin-npc-dialog` 分派位置早于宽泛的通用 `type` 处理器，避免再次被 `26/26` 截走。

当前已有专用正文的 actor id：

| actor id | NPC | 正文来源 |
|---:|---|---|
| 20020 | 欧冶子 | 当前服务端基础对白 |
| 20021 | 小猴子 | 当前服务端基础对白 |
| 20090 | 王朝 | `04临安王朝.xse` 常态对白 |
| 20091 | 马汉 | `04临安马汉.xse` 常态对白 |
| 20092 | 胡斐 | `04临安胡斐.xse` 常态对白 |

XSE 导出仅用于人工恢复正文和记录证据；运行时响应不依赖临时导出文件。

## 验证

- `make -j2` 通过；
- 场景归属校正前曾在临安-北宣门点击王朝并验证 `26/1` 对话协议，服务端记录：

```text
mock_npc_dialog actor=20090 index=0 name=王朝 script=04临安王朝.xse scene=c04临安府_09 catalog_match=1 dialog_len=68 resp=101 evidence=JianghuOL.CBE:0x01037ED4+0x010380E8
net_send connect=0 wt=26/1 len=30 source=builtin-npc-dialog resp=101
```

- 该记录只验证客户端原生对话页和包结构：中文正文正确换行，持续显示期间无地址访问异常、算术异常或断言失败。2026-07-17 已确认王朝、马汉、胡斐的当前目录归属应为临安-南宣门 `c04临安府_01(.sce)`；旧北宣门日志不再作为位置证据。

验证日志：`tmp/mock-service-19090.20260717-174239.stdout.log`。验证截图：`bin/multiplayer-data/player-1/autotest/screens/000035_00105231.bmp`。
