# 切磋邀请名称字符串边界

Date: 2026-08-19

Status: implemented-and-regression-covered

## 1. 现象与第一次偏离

切磋邀请确认框中的玩家名称后面出现随机字符。第一次偏离发生在服务端
`4/15.name` 字段的编码：切磋邀请使用了不带结尾 NUL 的普通字符串，而同类
组队邀请 `5/2.name` 已使用带 NUL 的 C 字符串。

## 2. 客户端证据

`江湖OL.CBE:net_handle_login_or_name_result(0x0101258A)` 的 subtype 15 分支：

1. 通过对象虚表读取 `name` 指针（`0x01012688-0x01012698`）。
2. 将指针传给 `fmt_sprintf_like`（`0x0101269C-0x010126A0`），再创建原生切磋
   确认框。

该格式化路径按 C 字符串结束，不会把 WT 字段长度当作显示边界，因此没有 NUL
时会继续读取相邻字节，表现为随机尾部。

## 3. 服务端契约与修改

- 请求/通知：场景同步中的 `WT 1/4/15`，字段 `id:u32`、`name:string`。
- 修改点：`src/server/mock_server_social.c` 的
  `VM_MOCK_SERVICE_SOCIAL_NOTICE_SPAR_INVITE`。
- `name` 改为 `vm_net_mock_put_object_cstring()`，字段长度包含
  `strlen(name)+1`，payload 末尾恰好一个 NUL。
- 保留窄范围 `spar_invite_name_wire` 日志，记录原始字节长度和十六进制内容。

## 4. 回归断言

隔离脚本 `scripts/duel-round-barrier-regression.php` 解析 `4/15.name`，要求其
严格等于 `DuelJobOne\\0`，从而同时验证名称内容和终止边界。

## 5. 未解决项

切磋自然终局当前仍使用固件 `mmBattleMstarWqvga.cbm:sub_7BD0(0x7F06)` 的
`4/4(result=1)` 清理路径；固件内置文案明确为“逃跑成功”。切磋专用的非逃跑终局
对象尚未从真实协议包确认，本修复不改变该语义。
