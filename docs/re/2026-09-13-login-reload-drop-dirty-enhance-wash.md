# 登录强制 reload 冲掉未落库首获 wash（2026-09-13）

## 触发与现象

玩家背包刚获得带首获随机强化档的装备（`enhance_wash` 已在内存、
`inventory_dirty` 待 deferred flush），杀客户端进程后立刻再登录：
强化附加变回默认黄字 / 空覆盖。

## 业务链路

1. `add_backpack` seed → 内存 `enhance_wash` + `mark_inventory_dirty`
2. 权威落库原计划：CBMR 后异步 persist / 断线 sync flush
3. 杀进程后重登 → 标题 `1/1/4` 角色列表：
   `role_db_loaded=false` → `vm_net_mock_role_db_load()` 从 MySQL 整表覆盖堆
4. 若此时 MySQL 尚无 dirty 快照 → 首获 wash 从内存与库两侧同时消失

## 首次偏离

`build_title_role_list_actorinfo` 为修 sticky 空列表强制 reload，
但在覆盖堆前**未**把仍有效的 dirty 内存先 `role_db_save`。
同会话内存里其实还有 wash，却被登录路径主动丢掉。

## 根因

登录强制 MySQL reload 与 deferred inventory flush 的时序竞态：
reload 优先于「未落库堆状态」提交。

## 修改

1. `vm_net_mock_role_db_load`：在 `memset` 前，若
   `role_db_valid && roleCount>0 && (inv|pos)_dirty`，先 sync
   `role_db_save("flush-before-role-db-load-*")`。
2. 角色选择失败的「污染堆」重载路径：先清 `role_db_valid` 与 dirty，
   **不**把不可信堆刷进 MySQL。

## 验证

- [ ] 获得首获随机装备后立刻杀进程再登录：详情四档仍在
- [ ] 正常断线重登 / 标题回登录再进：无丢档、无双份装备
- [ ] 日志可见 `role_db_flush_before_reload`（有 dirty 时）
- [ ] `make -j2` 通过

## 仍未知 / 风险

- 进程（服务端）被杀时内存全无，只能依赖此前已完成的 MySQL 写入。
- 历史上已被空 wash 覆盖的行无法靠本修复回滚。
