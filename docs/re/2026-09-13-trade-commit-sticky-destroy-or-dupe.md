# 交易黑铁宽剑变成幻火双刃+1（2026-09-13）

## 触发与现象

`lxh001/蔡文姬(role=1)` 将管理员发放的黑铁宽剑（`itemId=1002`，seq≈3156）
与 `lxh002/青云仙子(role=928)` 多次成交后，接收方背包里看到的是已有的
幻火双刃+1（`itemId=40116`），黑铁宽剑从双方背包与装备栏消失。

## 业务链路

1. `21/5` 报价：客户端只报 `seq+count`；服务端 `trade_validate_offer` 按 seq
   从背包拷贝 `itemId/enhance/wash/instance`。
2. `trade_commit`：双方内存拷贝 → consume → add → `trade_persist_pair`（MySQL）
   → `*liveRoles = roles` → `role_db_save("trade-commit-immediate")`（account
   capture/restore 切换）→ `note_durable_role_write`。
3. 客户端成交后 `openBag()`，以服务端快照为准。

## 运行时证据

| 成交 | 确认方(active) | 期望 | 开包 grid |
|------|----------------|------|-----------|
| #1 role1→928 给装备 | lxh001（卖方） | 1:−1, 928:+1 | 1 仍 24；928 36→37（复制） |
| #2 928→role1 归还 | lxh001（买方） | 928:−1, 1:+1 | 928 37→36；1 仍 24（销毁） |
| #3 role1→928 再给 | lxh002（买方） | 1:−1, 928:+1 | 1 24→23；928 仍 36（销毁） |

MySQL 复核：双方无 `1002`；`role=1` 仍残留 `enhance_wash_record` seq=3156
的黑铁首获 seed；`role=928` 仅有既有 `40116`（含 9/12 的 wash 行）。

结论：**不是 itemId 被改写成 40116**，而是成交后黑铁实例被 sticky 回写弄丢，
玩家把接收方原有幻火当成“交易结果”。

## 首次偏离 / 根因

`trade_role_for_session`：当前 active 账号的 live 角色指向
`g_vm_net_mock_role_db`；对端指向 `account->roleDb`。

成交写回 live 后，`trade-commit-immediate` 循环会：

1. `account_capture(active)` — 若 inventory 未 dirty，**sticky skip**，不把
   已更新的 globals 封进 `account->roleDb`（仍为成交前快照）；
2. `restore(peer)` → `role_db_save(peer)`（对端正确）；
3. `restore(active)` — 用**过期** `account->roleDb` 覆盖 globals；
4. 随后对该 active 的 `role_db_save` / 会话继续使用被覆盖的背包，
   **盖掉**刚成功的 `trade_persist_pair`。

故：

- active = 卖方 → 卖方背包被恢复 → **物品复制**；
- active = 买方 → 买方背包被恢复 → **物品销毁**。

`note_durable_role_write("trade-commit")` 原先在 immediate save **之后**，
无法阻止第 3 步的过期 restore。

## 修改

1. 在 `*liveRoles = roles` 之后、`trade-commit-immediate` 之前，对双方调用
   `note_durable_role_write(..., "trade-commit-seal")`，强制把成交后背包封进
   `account_state.roleDb`。
2. immediate save 后再 `note_durable_role_write("trade-commit")` 一次。
3. 增加 `mock_trade_offer_item` / `trade_commit_receipt` 日志（seq/itemId/
   enhance/instance），便于以后核对。

## 验证

- [ ] 重启 mock server 后：A 发一件唯一 `1002` 给 B（B 先确认 / A 先确认各一次）
- [ ] 每次成交后双方 `mock_pixi_bag_open` grid 与 MySQL `item_id=1002` 行一致
- [ ] 日志出现 `trade_commit_seal` 与 `trade_commit_receipt ... item=1002`
- [ ] `make -j2` 通过

## 仍未知 / 风险

- 历史已销毁的黑铁无法从 wash_record 自动回滚到背包（record 无 account 维度
  且实例可能已复用）。
- 其他同样「先改 globals、再 capture-skip + restore 对端」的双账号写路径需
  单独审计（拍卖交割等）。
