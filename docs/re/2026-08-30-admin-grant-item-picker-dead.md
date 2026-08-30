# 账号管理「给予物品」成功后物品下拉失效（2026-08-30）

## 触发条件

网页后台「账号管理」→「给予物品」：选物品并提交成功（toast「物品给予成功」）后，再点物品选择按钮，弹层不再出现；刷新整页后恢复。

## 首次偏离

`grant-item` 走 AJAX：`setupAjaxForms` 成功后调用 `refreshRoleDetails`，用新 HTML 的 `#role-details` 整段 `replaceWith` 旧节点。

物品选择器 modal、触发按钮和 hidden input 都在 `#role-details` 内（见 `vm_mock_admin_render_item_grant_form`）。替换后：

1. 旧 DOM 上的 click / 关闭 / 过滤监听一并销毁；
2. `setupItemPicker` 只在 `DOMContentLoaded` 执行一次，未对新区段重新绑定；
3. 新触发按钮无监听，表现为“点了没反应”。

同段替换也会弄丢外观解锁表单的 `setupCosmeticGrant` 绑定。

## 根因

AJAX 局部刷新角色明细后，未重新初始化依赖该 DOM 的客户端控件。

## 修改点

`src/web_admin_server.c` 内联管理脚本：

- `refreshRoleDetails` 在 `replaceWith` 后调用 `setupItemPicker()`、`setupCosmeticGrant()`，并清掉残留 `modal-open`；
- `setupItemPicker` / `setupCosmeticGrant` 对当前 modal/控件做一次性就绪标记，避免同节点重复绑定；document 级 Escape / sync 只挂一次；表单必选校验按 form 去重。

## 验证

- 手工：同一账号连续给予两次不同物品，第二次仍能打开物品目录并提交。
- `make -j2`：通过。

## 风险

- 其它也会 `refreshRoleDetails` 的动作（加钱、改名、洗练等）同样受益；若未来把选择器移出 `#role-details`，就绪标记仍有效，但刷新后不必再 setup。
