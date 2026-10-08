# 2026-10-05 CBE 端点取消空账号自动注册

## 触发条件

CBE 客户端标题「开始」走 `WT 1/1/12`，且 `username`/`password` 为空（首次未保存登录记录）。

## 原行为

CBE mock-service 端点在 `vm_mock_service_login_is_no_account` 命中后调用
`vm_mock_service_account_issue_guest_credentials`，落库 `guestXXXXX` 并在
subtype-12 `result=3` 响应里回写账号密码。

## 修改

- `src/server/mock_server_transport.c`：空凭证 `1/1/12` 改为登录失败
  （`result=2`，`information`=请先登录游戏），不再绑定会话、不再签发 guest。
- `src/server/mock_server_role.c`：删除 guest 签发函数。
- 网页注册、`account create`、已有账号密码登录不变。

## 验证

- `make -j2`：见本次修改后的构建。
- 运行时：空账号点开始应变失败提示；已注册账号正常登录。
