# 壶储量与战后场景 HP/MP 不一致（2026-08-09）

## 触发与首个偏离

自动挂机结算会把 802「神仙壶」与 803「逍遥壶」的剩余生命/法力储量通过 `7/11.info`
按背包序号同步到客户端。此前角色加载时的通用背包归一化把这两个物品的 `item_count`
误作普通 `stack=1` 的堆叠数量：一个储量为 50,000 的壶会被尝试拆成 50,000 个序号行。

容量不足时服务端保留原行却反复打印 `backpack_stack_limit_unresolved`；容量足够的测试数据则
真的生成多个行。两种情况都违反了客户端的壶合同：802/803 的 `item_count` 是单个容器的
32 位剩余储量，而不是可见数量。后续 `7/11` 可能命中与画面中容器不同的序号，导致战后
自动恢复、背包储量与场景 HUD 的状态来源分歧。

## 客户端与资源证据

- `item.dsh`：802 的容量是 50,000 HP，803 的容量是 50,000 MP，`consumeMode=2`。
- `JianghuOL.CBE:0x01039952` 的 `30/21` 行只接受可见数量 `1`；
  `0x01033544` 的 `7/11.info` 才按 `i16 seq + u32 value` 写入 802 的 HP 储量或 803 的 MP 储量。
- `mmBattle:BattleSettle_UpdateCharAttrs(0x2C50)` 在结算退出时将 `4/7.hp/mp` 增量写入场景主
  角色节点 `+0xB4/+0xB8`；`scene_draw_status_panels(0x0101466A)` 直接读取这两个字段绘制顶部
  HP/MP 条。

因此必须保证一个持久化壶行始终对应一个客户端背包序号，不能用通用堆叠规则拆分。

## 修复

`src/server/mock_server_role.c:vm_net_mock_role_normalize_backpack` 现在显式排除 802/803：

- 仍保留为一个实体背包行；
- `count` 原样保存为该实体的剩余 HP/MP 储量；
- 登录时由已有的 `30/21 (wire count=1) + 7/11 (full reservoir)` 协议恢复；
- 战斗自动恢复复用同一实体序号，并在终局 `4/7 + 7/11` 同步。

这不是对 HUD 的兜底写入，也没有改动客户内存或包内容；修复点是最早违反的持久化背包语义。

## 隔离回归

`hangup-auto-vitals-flask-v1` 使用独立 MySQL schema、端口和客户端副本，放入：

- 802，剩余 HP 储量 15；
- 803，剩余 MP 储量 10；
- 角色初始 HP/MP `80/295、70/205`。

运行产物：
`artifacts/automation/hangup-auto-vitals-flask-v1-20260809T121905756Z-21384/`。

关键证据：

```text
mock_backpack_grid ... gridnum=2 stored_rows=2
mock_backpack_reservoir_seed ... rows=2 ... response=7/11
mock_battle_terminal_action_vitals ... hp=95/295 mp=80/205
mock_battle_settle ... vitals=95/295,80/205 recover=15/10
mock_battle_auto_flask ... hp=15 mp=10 rows=2 response=4/7+7/11
automation_hangup_vital_scene_observed ... battleHp=95/295 battleMp=80/205
```

客户端通过正常硬件输入进入挂机、正常解析终局包、正常发送 `25/5` 退出结果面板；断言仅只读
场景节点，未修改客户状态。
