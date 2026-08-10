# PixiJS 首次登录误报 account or password error

日期：2026-08-10

## 症状

账号密码正确，第一次点登录提示「登录失败：account or password error」，再点一次才成功。

## 根因（客户端优先）

登录页用透明 `<input>`（`LoginIme`）接软键盘/自动填充，画布状态 `userValue`/`passValue` 靠 `input` 事件同步。

首次偏离：

1. 软键 / `touchstart` 的「确定」常在最后一个 `input`（或自动填充的 `change`）之前触发 `TitleScreen.confirm`。
2. `confirm` 读到的仍是旧的 `passValue`（空、localStorage 旧值、或缺最后几位）。
3. 服务端如实返回 `result=2 information=account or password error`。
4. 再点一次时 DOM 已同步，密码正确。

浏览器自动填充密码时经常**不发 `input`**，直到 blur/`change`，同一竞态更明显。

次要：账号查找 MySQL 查询瞬时失败时，`find_record` 返回 NULL，也被映射成同一句「account or password error」（与真错密码不可分）。

## 修改

- `LoginIme.readValues()` + `change` 监听；`confirm` 在 overlays 可见时以 DOM 为权威再提交。
- `account_find_record`：COM_QUERY 失败重试一次；鉴权前若账号库无效则报 `account db unavailable`。

## 验证

1. 登录页输入/自动填充密码后立刻点左软键「确定」：应一次成功，不应先报密码错误。
2. 故意输错密码：仍报 `account or password error`。
3. MySQL 不可用：应见 `account db unavailable`，而不是密码错误。
