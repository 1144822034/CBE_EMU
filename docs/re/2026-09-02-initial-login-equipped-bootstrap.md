# 首次登录已装备列表为空

## 现象与首次偏离

角色完成选角并进入场景后，人物信息的装备页“已装备”列表为空；这不是装备数据库被清空。

本地 `player-3` 会话的 `bin/server_out.txt` 记录了选角后的真实请求顺序：

1. `WT 1/5/10 + 1/7/7{type=1}`；
2. 随后的独立 `WT 1/7/7{type=2}`；
3. 随后的独立 `WT 1/7/7{type=3}`。

旧实现对第一个组合请求仅返回分组对象，并把 `30/21` 背包网格延后到第二步；日志存在
`mock_backpack_grid` 和 `mock_backpack_reservoir_seed`，但没有任何已装备 `1/7/7{type=2}`
或完成 `type=3`。这是客户端物品管理器首次缺少已装备实例的最早偏离点。

历史的原始抓包
`bin/multiplayer-data/player-3/logs/login-equipment-packet-capture/run-00015983-00046708-001/manifest.tsv`
给出了工作契约：对第一个组合请求的下行包含 `30/21`、`7/11`、`1/7/7{type=2}`
及装备流结束对象；不是点击人物装备页后才由 `29/4` 请求补齐。当前会话也未观察到该自查
页面发送 `29/4`，因此没有增加无证据的页面查询兜底。

## 客户端证据与根因

`mmGameMstarWqvga.cbm:sub_D04` 的 `7/7 type=2` 分支按固定装备槽序号构造耐久、强化和词条齐全的
已穿戴实例；`type=3` 的空 `iteminfo` 分支结束该流并走共享状态重建。仅返回 `30/21` 会创建背包
物品，不能替代此已装备实例流。

此前将该流从首次组合响应中移除后，客户端不会建立已穿戴列表，因此界面为空。另一方面，
`docs/re/2026-08-29-player1-shop-return-ingame-assert.md` 已证明：同角色从商城返回时重放
`type=2` 是向已存在物品管理器重复插入，可能触发 `MMORPG_Screen_InGame.c:913`。根因不是该流
本身无效，而是两个客户端生命周期被错误地共用了一条发送路径。

## 修复

- `vm_net_mock_build_title_role_select_response()` 在成功选角时设置仅属于当前会话和角色的
  `initialEquipmentBootstrap` 标记，并清除可能遗留的延后背包阶段。
- `vm_net_mock_append_backpack_role_grid_main_objects()` 仅在该标记命中时，把完整 `30/21`、`7/11`、
  `1/7/7{type=2}` 和空 `1/7/7{type=3}` 依次写进同一真实 group/type-1 响应；成功后立即消费标记。
- 同角色商城返回不设置该标记，仍使用既有的延后 `30/21` 生命周期，因而不会重放已装备行。

服务端只针对客户端已发出的请求构造 WT 对象；没有修改 CBE/CBM、客户内存、寄存器、回调或宿主
事件顺序。

## 验证

已执行：

```text
make -j2
make initial-login-equipped-bootstrap-regression
.\obj\server\initial-login-equipped-bootstrap-regression.exe
```

隔离回归使用内存会话和角色，不启动监听器、数据库或客户端，直接调用生产 group/type-1 构造器并验证：

1. 首次标记产生恰好一组 `7/7(type=2)`，且其顺序早于恰好一组 `7/7(type=3)`；
2. 标记已消费且网格种子已完成；
3. 随后的无首次标记网格引导不再携带任一 `type=2/type=3` 装备对象。

仍需重启本地 `jh-online-server.exe` 后由 player-3 人工验收：重新登录同一角色，进入人物信息的装备页，
确认已装备列表出现；随后走一次商城返回，确认不会出现 `InGame.c:913` 断言或重复装备。
