# 账号管理：调整铜钱支持负数扣除

## 需求

管理后台「账号管理 → 角色明细」调整普通钱币（铜钱）时，应与 W 币一致：
正数增加、负数扣除。

## 根因 / 契约

原先 `add-money` 表单 `min=1`，action 用 `parse_u32_strict`，服务
`vm_mock_service_account_add_role_money` 只接受 `u32 amount` 做加法。
W 币路径已支持 signed delta。

## 修改

- 表单：与「调整 W 币」相同，允许 `-2147483648..2147483647`，按钮改为「调整铜钱」。
- action：`parse_i32_strict`，非零；成功文案区分增加/扣除。
- `account_add_role_money`：参数改为 `int32_t delta`；不足时返回「铜钱不足，无法扣除」，
  不写成负余额；scoped save 原因 `admin-money-add` / `admin-money-deduct`。

## 验证

- `make -j2`
- 后台对某角色输入正数 → 铜钱增加；输入负数且余额足够 → 扣除；余额不足 → 错误提示。
