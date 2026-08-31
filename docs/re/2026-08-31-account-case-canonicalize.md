# 账号大小写：登录字面量 vs ascii_bin 主键

日期：2026-08-31

## 现象

`AA862047` 注册并存角色 `role_id=1447`，服务端刷屏：

```text
role_deferred_flush_failed account=aa862047 reason=role-deferred-inventory async=1
```

MySQL 仅有 `HEX(account_id)=4141383632303437`（`AA862047`），无小写行。

## 根因

1. `accounts.account_id` / 角色表均为 `ascii COLLATE ascii_bin`，主键大小写敏感。
2. 会话绑定原先使用**客户端登录原文**（`loginRequest.userName`），不是 `SELECT account_id` 返回的库内拼写。
3. Pixi 登录框 `autocapitalize=off`，手机键盘默认小写，玩家常输入 `aa862047`。
4. 若认证仍以错误大小写进入会话，延后落库会带着 `role_id=1447` 写到小写 `account_id`，撞上全局唯一 `uk_account_roles_role_id` → flush 失败 → dirty 不清 → 重试刷屏。

代码里**没有**主动 `tolower` 账号字符串；“变成小写”来自输入面，服务端按字面落库放大了问题。

## 修改

| 点 | 行为 |
| --- | --- |
| `account_find_record` | 先精确匹配，再 `LOWER(account_id)=LOWER(...)`；返回库内拼写 |
| 登录 bind | 用 canonical `record->username` 绑定会话，并打 `account_case_canonicalized` |
| 账号堆 find / find_or_create / takeover / same-account | ASCII 大小写不敏感相等；创建堆行时优先库内拼写 |

## 验证

1. `make -j2`
2. 用小写 `aa862047` 登录已注册的 `AA862047`：应出现 `account_case_canonicalized typed=aa862047 canonical=AA862047`，随后 flush 日志为 `account=AA862047`，不再 `role_deferred_flush_failed` 刷屏。
3. 注册仍保留用户输入的大小写；不能再注册仅大小写不同的第二账号（fold 查找会判已存在）。
