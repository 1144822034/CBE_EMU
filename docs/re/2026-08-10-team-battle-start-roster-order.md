# 组队战斗进入崩溃：队伍名册与 `4/5` 启动包顺序

## 触发与原始证据

两个在线角色在同一场景完成组队后，队长立即触碰场景怪物并发送 `WT 4/1`。
客户端接收长度为 215 的响应后崩溃：

```text
queue_data connect=2 event=7 resp=215
PC=00000000, LR=05032435, lastPc=05032432
```

这不是以 `PC=0` 为根因的判断；它表示后续绘制/状态机跳转读取了未初始化的
回调入口。

同一轮 `bin/server_out.txt` 的顺序为：

1. 受邀者收到成功 `5/3`，其中只有队长名册行；
2. 服务端将给队长的 `TEAM_RESULT`（`5/4`）和
   `TEAM_MEMBER_JOIN`（`5/5`）放入 scene-poll 通知队列；
3. 队长在这两条队列通知尚未投递前发送 `4/1`；
4. 服务端直接构造了含两条右侧角色记录的 `1/4/5`，并记录
   `mock_team_battle_start ... response=215`。

日志在 `mock_team_battle_start` 之前没有
`team_result_deliver observer=<leader>` 或
`team_member_join_deliver observer=<leader>`；因此服务端知道队伍已有两人，
但队长客户端尚未拥有第二名队员的本地队伍表行。

## 已确认的客户端契约

按 `binary_name=mmBattleMstarWqvga.cbm` 选择 IDA 实例后：

- `HandleServerBattleCmd(0x7BD0)` 将 subtype `5` 交给
  `HandleBattleStartMsg(0x66CC)`；
- subtype 5 的 battleinfo 先读左侧场景怪物元组，再读右侧人数与每条
  `{id,hp,hpMax,mp,mpMax}`；
- `HandleBattleStartMsg(0x66CC)` 对每个右侧 `id` 调用
  `sub_66A4(0x66A4)`；后者仅在从主 CBE 导入的四条队伍名册行
  `teamRow+0x24` 中查找；
- 找不到 id 时，`0x66CC` 不初始化该右侧战斗单位的头像/actor 回调，
  并把 battle-ready 置为 0。后续使用该单位即会表现为本次的空地址跳转。

场景怪物元组在这次响应中是 `index=8,pos=(146,349)`，与原始 `4/1` 请求
完全一致；已排除“以错误场景怪物行启动”是本次的第一次偏离。

## 根因

组队战斗的服务端启动条件只检查了服务端 `team->memberCount` 与同场景可见性，
却没有检查客户端是否已经收到形成同一组队表所必需的原生名册增量。
于是先发送包含两条右侧战斗记录的 `4/5`，后发送（或尚未发送）第二名成员的
`5/4 -> 5/5`。这违反了 `0x66CC` 先导入 CBE 名册、再按 id 构造右侧战斗单位的
顺序契约。

## 修复原则

服务端不得伪造角色模板、修改客户端状态或把找不到的角色退化为默认单位。
对尚留在服务端通知队列中的、属于当前队伍的原生名册变更，应在同一响应中按
`5/4`（同意结果）后 `5/5`（新增成员）的正常语义先行投递，再追加原有
`2/2 + 4/5` 启动对象；已投递的通知不重复发送。

被动队员的 scene-poll `4/5` 也必须使用相同前置步骤，以免三人以上队伍中某个
观察端尚未收到新增成员时重现相同错误。

## 实现

- `src/server/mock_server_social.c` 新增受限的
  `vm_net_mock_append_team_battle_roster_preamble()`：只提取属于当前队伍的
  `TEAM_RESULT(result=1)` 与 `TEAM_MEMBER_JOIN` 通知，严格按 `5/4` 再
  `5/5` 输出；它不会消耗聊天、交易或无关的社交通知。
- `src/server/mock_server_battle.c` 在队长的 `4/1` 直回和被动队员的待投递
  战斗开始包中，都先调用该 helper，再追加原有的场景怪物 `2/2` 与 `4/5`。
  WT 包对象数同步包含这些前置对象，已写出的通知立即从队列删除，避免之后的
  scene poll 再追加一次成员行。

## 构建验证

2026-08-10：`make -j2` 通过（Windows client/service target；本次重编译了
服务端聚合单元并生成 `bin/jh-online-server.exe`）。尚待用原始“两人同意后立即
触怪”的客户端路径验证运行时 parser 与两端战斗 UI。

## 验证

修改后需执行 `make -j2`，并覆盖：

- 同意后立即由队长触怪；
- 等待普通 scene poll 后再触怪；
- 两人和三人同场景队伍；
- 队员切图、离线或离队时不投递组队战斗开始包。

成功条件是两个客户端均完成 `0x66CC` 的右侧角色解析并进入战斗，且不出现
`PC=0`、未初始化角色或重复 `5/5` 名册行。
