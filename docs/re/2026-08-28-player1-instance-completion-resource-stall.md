# player-1 副本完结确认后资源更新停滞

## 状态

`validated-current-run; original-anomaly-not-reproduced`（2026-08-28）。本轮仅进行协议取证；不会通过
修改 CBE/CBM 指令、客户机内存、寄存器、PC/LR、screen 生命周期或伪造下载完成来关闭“正在更新资源文件”。

## 触发与首次偏离

- 客户端：`bin/multiplayer-data/player-1`，服务端会话 `b52977e2`，角色 `10871`。
- player-1 在黑木崖场景战斗中击败敌人后，看到“副本已完结”提示；点击确认后进入“正在更新资源文件”
  且不再前进。
- `bin/server_out.txt` 的最后一次正常战斗链路是 `WT4/2 -> 4/6`，随后
  `mock_battle_reward_panel_closed ... source=native-25/5`；它没有崩溃或重复进入场景。
- 紧接着首次异常请求为长度 73 的 `WT2/3`。场景 target 探测记录四次
  `mock_scene_target_rejected map=\x18 exit=9 reason=noncanonical-scene-key`；该请求因同时拥有
  `1/27/4` 被通用 `builtin-type27-followup` 以 96 字节响应处理。
- 随后客户端请求 `WT18/7`，资源名同为单字节 `\x18`；服务端记录
  `mock_update_chunk_missing subtype=7 file=\x18 ... resp=0`。这是资源更新界面停滞的第一处可见
  结果，而非根因。

## 已知契约与排除项

- `vm_net_mock_get_scene_change_target()` 只接受完整、无路径分隔符的 `.sce` 键；源码明确禁止将
  无效键替换成默认地图，避免错误移动角色。因此不能以 `mapID=\x18` 直接构造场景进入或资源文件。
- 普通门户进入麒麟殿已在本轮之前修复并复测：本运行仍显示一次 `30/2-posinfo`、再由 `WT6/1`
  完成，之后可正常进入战斗。当前问题不从该资源路径开始。
- `WT18/7` 的零字节响应无法向客户端证明资源完成；为它补一个任意资源或“成功”包会掩盖前一条
  `WT2/3` 所属业务相位。

## 当前假设与下一步

`WT2/3` 不是普通场景切换，而是副本结束确认后的复合请求；现有场景 change builder 因其包含一个
`mapID` 字段而探测它，随后 type27 fallback 又接管了请求。尚未知它的精确 object 顺序、所有字段以及
客户端 `0x18` 的数据来源。

## 新增取证与复现条件

`src/server/mock_server_scene_task.c` 现仅在 `mapID` 已被完整场景键契约拒绝时输出
`mock_scene_target_rejected_request`：记录 WT 头、最多八个对象的 `major/kind/subtype:payloadLen`、
字段存在性和最多 256 字节十六进制数据。该函数没有返回值，也不触及 dispatcher、响应缓冲区、
场景目标或客户端状态；重复的 detector probe 可能记录同一请求多次。

用更新后的服务端让同一 player-1 再走一次“最后一怪胜利 → 副本已完结 → 确认”。随后以该 trace 的
对象顺序和原始字段与客户端 parser/已有 IDA 记录对齐，再决定真正处理该副本结束复合请求的 handler。
在此证据出现前，不为 `WT18/7` 伪造资源完成，也不为 `mapID=0x18` 补默认场景。

## 同日 player-1 再测：正常退出

- 新会话 `6bd5003a`、同账号 `a1156296150`、角色 `10871` 在最后一场 `battle=5` 关闭奖励面板后，
  发送的目标已是完整 `c04…_08.sce`、`exit=9` 的 `WT2/3 len=88`，由 `builtin-scene-change` 返回 168 字节。
- 随后的 `WT25/5 len=93` 由 `builtin-type27-followup` 返回 136 字节；`WT6/1 len=39` 再由
  `builtin-scene-resource-followup` 返回 497 字节。服务端依次记录
  `mock_scene_change_initial_enter ... response=30/2-posinfo`、
  `scene_ready ... reason=scene-target-complete` 与
  `mock_scene_resource_positioned_portal_complete ... completion=resources+30/2-no-posinfo`。
- player-1 的 `actor-resource-cache.log` 记录 `JHOnlineData/c04…_08.sce` 的 `file-open-ready`，证明 CBE
  实际打开了正确的场景资源；本次没有 `mock_scene_target_rejected_request` 或
  `mock_update_chunk_missing file=0x18`。

因此这次退出并非仅服务端响应非零，而是客户端资源打开、场景进入和运行时完成链都已走通。先前
`mapID=0x18` 的来源仍未获得原始对象字节，当前无法证明它由哪一方状态造成；只在异常键出现时才触发的
取证日志会保留，用于下一次复现时定位，而不会影响正常副本退出。

## 本轮构建与回归

- `make -j2`：通过。
- `obj/server/scene-battle-monster-field18-regression.exe`：通过；确认场景怪物实体与正常战斗结算对象
  未受影响。
- `obj/server/scene-transition-entry-contract-regression.exe`：通过；确认普通门户、麒麟殿门户和真实的
  `WT18/7 -> WT6/1 -> 30/2` 资源完成链仍按既有契约收束。

这些回归不覆盖本文件记录的异常副本结束包，因其尚未获得完整请求字节；该路径的验证边界是下一次
player-1 复现产生 `mock_scene_target_rejected_request` 为止。
