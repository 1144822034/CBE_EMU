# 挂机战斗的运行时场景节点序号（2026-08-09）

## 触发与首个偏离

触发步骤：在 `01桃花岛_01.sce` 开启场景挂机，服务端响应
`WT 2/10(Type=2) + 25/3` 的首场战斗。

旧服务端从 SCE 中找到第一条 `actor_id=105` 战斗记录
`(295,57)`，但把它在**战斗记录列表**中的序号 `1` 写入
`WT 1/4/5.battleinfo.sceneMonsterIndex`。客户端在同一张图实际已加载的
场景表并不是该列表：行 1 是第一丛花 `b_flowers01.actor`，坐标
`(65,39)`，而毒泥怪位于行 6。

因此最早违反的契约是：服务器把 SCE 战斗记录序号当作客户端完整静态
场景节点表的序号。后续 `mmBattle` 错取了行 1；坐标不匹配后扫描有效
`kind=2` 节点又因目标在那一刻尚未 active 而失败，最终以错误节点构造
左侧战斗单位。其第一次绘制最终在
`江湖OL.CBE:DrawMapTileLayer(0x01004E9C)+0x0C` 对空指针解引用，表现为
访问地址 `0x0000000C`。崩溃 PC 是症状，不是根因。

## 证据

- 用户故障包：`bin/multiplayer-data/player-3/logs/hangup-protocol.log`
  的序号 527 为 248 字节，且对象顺序固定为
  `2/10, 2/2, 4/5, 4/11`。`4/5` 中的旧 index 为 `1`、坐标为
  `(295,57)`。
- 同一日志在 `mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` 的
  现场显示：先按 `target=1` 取到 `kind=3,pos=(65,39)`，之后在
  `0x67BA` 才定位到与坐标对应的 `target=6,kind=2,pos=(295,57)`。
  这证明运行时行号为 6，不是 1。
- 同场景正常碰撞请求已给出真实元组：
  `index=6,pos=(295,57)` 与 `index=8,pos=(146,349)`；服务端日志保留在
  `bin/server_out.txt`。
- 服务器权威资源 `bin/JHOnlineData/01桃花岛_01.sce`（同 `web/fs`
  发布源）以 5 条 `b_flowers01.actor` 摆放开始，之后依次为四条
  `actor_id=105` 战斗生成点：`(295,57)`、`(179,120)`、`(146,349)`、
  `(292,484)`。故完整静态节点号依次为 `6,7,8,9`。
- `mmBattleMstarWqvga.cbm:0x66CC` 的反汇编先用输入 index 以 0x154
  字节步长索引 25 行场景表；索引行的 `(x,y)` 不匹配时，仅扫描
  `active && kind==2 && node+240/+244 == 输入坐标` 的行。它没有对
  未找到的情况建立安全战斗单位。
- `JianghuOL.CBE:scene_node_update_move_blob(0x01012A76)` 只更新已经
  active 的 actor-id 匹配行，不能把一个 inactive 静态怪物节点变成
  可供上述扫描使用的节点。因此前置 `2/2` 不能修复错误的 `4/5` index。

## 修复契约

`vm_net_mock_select_sce_combat_spawn()` 现在先严格解析 SCE2 开头的
prop-scatter 段，再用：

```text
runtime_scene_node_index = prop_placement_count + combat_spawn_ordinal
```

其中第一个静态摆放就是客户端表的行 1；行 0 是本地角色。选择器仅在
能解析服务器权威 SCE2 源，且得出的索引在 `1..24` 时成功。它返回的
index、坐标和怪物 id 被同一份 `2/2` seed 与 `4/5` battleinfo 共同使用。
正常碰撞 `1/4/1` 仍直接保留客户端请求的 live tuple，未改变。

这不是从客户端内存读取或修改客户端状态：序号只由服务端已发布、客户端
正常加载的 `.sce` 静态资源推导。

## 负面证据与已排除方案

- 不以 `4/10` 代替 `4/5`：该 subtype 的视觉字段是对手玩家职业/性别，
  不能表示 `e_mucusP.actor` 怪物，历史运行会显示玩家模型。
- 不依赖 `0x66CC` 的坐标扫描：它要求怪物节点已经 active，挂机首包前
  这个条件不成立，属于非确定性偶然成功而不是协议契约。
- 不下发伪造 `2/10 otherinfo` 来创建怪物：该 parser 是周围玩家节点路径，
  没有证据表明它是场景静态怪物的生成协议。
- 不返回 `25/11` 作为已进入挂机请求的常规失败：
  `HandleBattleEnterReq(0x01015E14)` 在请求前已写入“获取数据”状态，
  banner 不是该状态的退出响应。

## 验证要求

1. 首场挂机服务端日志必须显示
   `runtime_index=6 prop_nodes=5 combat_ordinal=1 pos=(295,57)`，而不是
   `index=1`。
2. 客户端 trace 在 `0x674E` 直接取行 6，且 `0x67BA` 不再因错误 index
   才修正目标；进入第一帧战斗不得触发 `0x01004EA8` 空指针。
3. 覆盖第二、三处同类生成点；第三条应推导为 runtime index 8。
4. 重复场景进入与自动续战仍只走 `2/2 + 4/5 + 4/11`，不混入玩家型
   `4/10` 或客户端内存依赖。
