# 武林盟主挂机“获取数据”停滞：原生 SCE2 短头解析（2026-08-11）

## 触发与首次偏离

1. 使用账号 `21642502` 的武林盟主角色（role `10036`）登录；
2. 角色位于 `05上古皇陵_02.sce`，位置 `(48,60)`；
3. 点击场景挂机。

服务端已从 `automonster.dsh` 精确匹配到该场景的怪物 `25`，但随后的原生场景
节点查找失败：

```text
mock_auto_monster_catalog total=104 source=automonster.dsh
mock_hangup_battle_start scene=05上古皇陵_02.sce table_scene=05上古皇陵_02.sce
enemy=25 action=no-verified-scene-spawn response=2/10+25/11
```

故障不在角色、场景名或挂机请求选择：第一次偏离发生在服务端读取同一场景的 SCE2
静态怪物节点之前。失败响应仅有空 `1/2/10` 与 `1/25/11` 横幅对象，缺少客户端进入
战斗所需的 `1/2/2 + 1/4/5`。

## 客户端契约

IDA 按 `binary_name=江湖OL.CBE` 选择实例。`HandleBattleEnterReq`
(`JianghuOL.CBE:0x01015E14`) 在发出 `WT 2/10` 的 `Type=2` 请求后立即把
客户端 battle-state 设为 `3`。`25/11` 只走信息横幅分支，不会建立 `4/5` 所需的
战斗场景节点。因此把这种失败包用于已经进入状态 3 的挂机按钮，会稳定停在“获取数据”；
这不是 UI 层应当吞掉的状态。

`mmBattle:HandleBattleStartMsg(0x66CC)` 需要 `4/5` 的静态场景节点索引。不能用
`4/10` 的玩家行结构替代怪物；该旧路线会把左侧敌人解码成玩家模型。

## 原始资源证据

服务器当前读取的原始资源为
`web/fs/JHOnlineData/05上古皇陵_02.sce`（与 `bin/JHOnlineData` 同内容）。
通过生产资源解码器得到：

```text
payload length=578, SCE2 payload start=32
bytes[32..43] = 01 00 01 00 00 00 00 00 07 00 02 00
```

前八字节是客户端原生的短 prop-scatter 头：

```text
u16 kind=1, u16 version=1, u16 placement_count=0, u16 template_count=0
```

它在 offset 40 结束，接着是 `kind=7` 的边界传送记录。此前服务端仅接受 12 字节的
extended 头，误把后续 `kind=7` 与坐标数据当成 extended 字段，因而拒绝 prop-section，
从未扫描任何 kind-3 战斗记录。

同一份资源用生产 `vm_net_mock_parse_sce_combat_spawn_at()` 解析可得到四个 kind-3
记录，其中三条是挂机应选的 `actor_id=25`：

```text
(203,195) e_corpsehead.actor / e_ghostfireG.actor
(160,340) e_corpsehead.actor / e_ghostfireG.actor
(63,246)  e_corpsehead.actor / e_ghostfireG.actor
```

`tools/inspect_sce.py` 早已以短头输出该 section 的 `end_offset=40`，与这次生产
解析器取证相符。

## 修复

`vm_net_mock_parse_sce_prop_scatter_at()` 现在精确支持两种已验证的 SCE2 头：

- short：`kind/version/placement_count/template_count`（8 字节）；
- extended：`kind/version/placement_count/scatter_group=1/reserved=0/template_count`
  （12 字节）。

extended 形式使用其 `reserved=0` 的结构性标记识别；短形式不会把后继 record 当作头
字段。二者随后使用相同的模板和 placement 校验。此改动只修复服务器对客户端原生资源的
解析，不改变客户端内存、挂起状态、请求、战斗对象格式或怪物选择策略。

## 回归

新增只读资源回归程序：
`scripts/native-scene-hangup-spawn-regression.c`。

它不启动服务端、不连接 MySQL、不操作客户端；只通过生产资源加载、SCE2 payload、prop
section 和 kind-3 parser 验证 `05上古皇陵_02.sce`，同时以
`00蓬莱仙岛_02.sce` 的 4 个 extended prop 节点保护已有格式。预期且已观察到：

```text
native-scene-hangup-spawn-v1 extended-props passed: scene=00蓬莱仙岛_02.sce props=4 scan=98
native-scene-hangup-spawn-v1 passed: scene=05上古皇陵_02.sce props=0 kind3=4 actor25=3
```

人工回归应确认同一账号同一场景点击挂机后，日志从
`action=no-verified-scene-spawn response=2/10+25/11` 变为
`target_source=sce-static-node-order`，响应含 `2/10+2/2+4/5`，并实际进入怪物战斗。
