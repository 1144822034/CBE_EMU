# 仓库凭证有耐久仍提示请购买

Date: 2026-07-28

Status: implemented (server)

```text
trigger: 背包 834 仍有 item_count 耐久，使用/取存却提示「请重新购买」
root: g_netMockWarehouseSessionArmed 等为进程全局；任意会话 offline 会清掉
      当前玩家的已武装仓库态
fix: warehouseSessionArmed / DialogPending 改为 per-client-session
```

## 根因

旧实现：

- `g_netMockWarehouseSessionArmed`
- `g_netMockWarehouseDialogPending`

任一客户端断线/session reset 都会清全局。玩家 A 已用凭证打开仓库后，若 B 下线或同进程其它会话重置，A 再点取回/存入会走 `warehouse-locked`，文案却是「请重新购买」，尽管背包 834 耐久仍在。

## 修改

1. 状态迁到 `vm_mock_service_client_session`
2. 834 使用成功：`vm_mock_service_session_arm_warehouse_pass_dialog`
3. wire/poll 第二帧 `26/1` 按 clientId 取 pending
4. locked 且背包仍有 834：提示「请先使用背包中的仓库凭证」；无耐久才「请重新购买」
5. 使用时 seq 漂移则回退查找任意 live 834

## 验证

```text
使用 834 → warehouse_pass_dialog_arm / mock_warehouse_pass_use
mock_warehouse_dialog_wire client=...
# 取回/存入正常；其它账号上下线不影响本会话
# 无误报「请重新购买」当 bag 仍有 834 count>0
```
