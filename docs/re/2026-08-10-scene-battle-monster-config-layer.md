# 场景战斗怪配置层

## 触发与目标

动态 NPC、任务发布者和场景战斗怪在客户端不是同一种实体。过去若把普通
NPC 的 `27/11` 下行记录当作怪物使用，场景中可以显示模型，但进入 `4/5`
场景战斗时 `mmBattle` 找不到可复制的真实怪物节点，后续战斗 UI、动画或
内存访问都不再满足客户端契约。

本层用于把运营配置部署为客户端原生的场景战斗节点；它不是动态 NPC 的
兼容模式。

## 客户端证据

- `JianghuOL.CBE:LoadSceneDataFromStream(0x01006204)` 解码 `SCE2` 资源并创建
  场景实体。
- `JianghuOL.CBE:scene_parse_npcinfo_and_spawn_npcs(0x01037998)` 处理的是
  `27/11` NPC 记录，得到的是 NPC 节点，不是场景怪物节点。
- `mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` 在 `4/5` 场景战斗开始
  时按战斗坐标扫描已加载的 type-2 场景节点，并复制对应节点。不能从普通
  NPC 或服务器私有字段合成替代物。

已验证的 SCE2 `kind=3` 战斗记录语法为：

```text
u16 kind=3, u16 x, u16 y,
u16 field=5, u16 value=1, u16 field=14, u16 monster_id,
string field=15 display_name,
u16 field=16, u16 visual_hint(5 或 6),
string field=17 actor_resource,
unnumbered child effect Actor (`u16 kind=3, u16 kind=3, u8 len, bytes`)
```

这正是部署器输出并重新解析验证的格式。

## 数据与部署契约

MySQL 表：

- `server_scene_battle_monsters`：运营草稿，唯一位置键为
  `(scene, monster_id, pos_x, pos_y)`。
- `server_scene_battle_monster_sources`：第一次部署时捕获的服务端原始 SCE
  基础字节；每次部署从基础重建，避免重复追加。
- `server_scene_battle_monster_deployments`：草稿指纹与最近一次部署状态。

后台“保存草稿”不会向任何客户端投递 NPC 或怪物包。显式“部署”会：

1. 校验场景、GBK 名称、坐标、视觉提示、主 Actor 与退场效果 Actor；并按已有
   资源发布流程确保这两项 Actor/GIF 依赖可发布。退场 Actor 是客户端创建 type-2
   场景节点的原生子记录，不是可以省略的装饰字段。
2. 解码捕获的基础 SCE，统计原有静态节点，并为当前场景最多四条初始
   `27/11` NPC 记录预留节点位。
3. 在原生最终 kind-8 场景控制记录之前（无此记录的场景则在 EOF）插入每个启用项的
   kind-3 记录，再以同一边界重新核验所有新增项。
4. 拒绝会超过客户端 25 个场景节点表上限的输出：本地角色以外的静态节点、
   已下发 NPC 与启用战斗怪合计最多为 24。
5. 使用普通资源格式（外层长度 + 标准 LZSS literal 流）写入**服务端**资源根，
   然后走已有 WT18/7 具名发布入口；发布失败时恢复先前服务端资源。
6. 使由 SCE 扫描得到的服务器怪物目录失效，让怪物管理在下一次正常查询时
   从部署后的真实 SCE 重新建立索引。

部署状态写入失败会明确报告“资源已经部署并发布”，不会伪装为未修改；重试
部署可补齐状态表。

## 缓存限制

WT18/7 具名资源发布只会在客户端请求缺失资源时下发。它不是已缓存同名 SCE
的强制覆盖通道。部署后的 SCE 会自动加入启动内容版本：客户端完整重启时，
WT18/9 返回内容 release，WT18/8 下发失效清单，随后正常 WT18/7 下载实际字节。
安装完成后再进入场景。服务端不会伪造一次场景重载或临时 NPC 包来绕过该限制。

## 任务“挑战小猴子”的正确设置

小猴子 `actor_id=20021` 是 `00蓬莱仙岛_02.sce` 的普通 NPC；`e_monkey.actor`
本身没有让该 NPC 变为 type-2 场景战斗节点。若任务语义是“击败小猴子”，需：

1. 在本配置层为同一场景新增一个独立战斗怪，选定稳定 `monster_id`、坐标和
   `e_monkey.actor`。
2. 在怪物管理中配置该 `monster_id` 的属性与掉落。
3. 将任务条件配置成类型 `2（击败怪物）`，目标 ID 为该 `monster_id`，而不是
   NPC actor ID 20021。

任务发布/提交仍绑定欧冶子等 NPC，战斗碰撞和 `4/5` 开战则由部署后的真实场景
怪物节点走原始客户端流程。

### NPC “挑战”选项与场景战斗怪

NPC 的 action13 挑战请求不是一律 `4/10`。`江湖OL.CBE:SendNPCInteractReq
(0x01037ED4)` 会按 action value 在当前场景节点表搜索并写入 `index`，再将
`posx/posy` 置零。对于 value 指向 kind-3 场景战斗怪的挑战，正确下行是 `4/5`：
`mmBattle:0x66CC` 用该 index 和服务器解析出的静态 spawn 坐标复制 type-2 节点的
Actor。`4/10` 不使用该 index，只能生成角色职业/性别模板，不能表示怪物 Actor。

这不允许服务端以离线 SCE 结果猜测客户端节点。`SendNPCInteractReq` 已在客户端以
目标 actor ID 扫描 active scene-node table；因此 action13 上行的 **nonzero index 本身**
就是该 live node 已存在的证据。服务端只校验配置敌人 ID、当前场景和时序，并使用请求
index 与 SCE 提供的静态 x/y 返回 `4/5`；不得以服务器按 prop/NPC 数量推导的 ordinal
覆盖或拒绝客户端 index。若客户端未找到目标而发出 0，才拒绝挑战，不能返回 `4/10`。

2026-08-10 的实际复现中，服务端将 `00蓬莱仙岛_02` 的小猴子配置解析为 index 8，
但本次客户端没有发送该 SCE 的 `18/7 clientmiss` 下载请求；随后 action13 上行也未
找到该节点（`index=0`）。服务端仍按离线 row 8 返回 `4/5`，使
`mmBattle:0x66CC` 从客户端旧场景的空 row 构造左侧单位，首帧在
`JianghuOL.CBE:0x01004EA8` 因视觉上下文为空崩溃。故 action13 不能以服务器 SCE
单方面推算 `4/5`；它应在已证明的客户端 live index 存在时使用 `4/5`，否则拒绝。

场景怪的 `4/5` 仅用于客户端真正碰撞后上行的 `4/1`：该请求的
`index/posx/posy` 是客户端活节点元组，服务端必须原样使用。若希望“直接挑战”也以
场景小猴子外观进入战斗，需要先找到客户端支持的**同名已缓存 SCE 更新/失效协议**，
并在下载完成后获得客户端实际 node tuple；在该协议尚未取证前，不以服务端资源发布
或节点计数作为其替代证明。

对于仅配置 `challenge_enemy_id`、没有 `target_scene` 的 NPC，首个对话可直接编码
客户端原生 `action=13`（“挑战守关怪”）。点击后客户端发送 `4/1`；只有当其 `index`
实际命中已加载的同一场景战斗怪时，服务端才以 `4/5` 开战。未命中时应报告场景战斗怪
尚未加载，不再用玩家模板 `4/10` 掩盖资源/节点不一致。若同时配置了目标场景，进入副本
与挑战是不同操作，仍保留原菜单。

## 验证

- `make -j2`：通过。
- `00蓬莱仙岛_02.sce` 的服务端原始资源（267 字节）已只读解码为 336 字节
  SCE2；开头 prop-scatter 的 placement_count 为 4。因此该基础场景单独部署
  一只战斗怪不可能因为 24 节点上限被拒绝。部署器会分别报告
  `load-base`、`decode-base`、`count-base` 与 `node-limit`，不再把这些原因
  合并为同一条错误。
- 2026-08-10 实际部署的 `count-base` 日志记录了 `raw=267 payload=267`，而
  非正确的 `payload=336`。根因是压缩资源的 literal 流中也含有 `SCE2` 字节：
  部署器先用宽松的 SCE 搜索判断“是否未压缩”，把资源包装误作 payload。解码器
  现改为先识别合法的四字节长度 + LZSS 包装并解压，再考虑未压缩 SCE；不会放宽
  节点上限，也不会修改基础快照。
- 本次没有对用户运行中的服务、客户端缓存、`jh_online` 内容配置或场景资源执行
  自动部署。首次实际部署应在测试场景上确认：客户端资源获取、进场 type-2 节点
  创建、触碰发出 `4/1`、随后 `4/5` 中的 live-node 匹配，以及战斗退出后重复进场。
- 本次根因修正后的预期日志为
  `mock_scene_monster_target ... prop_nodes=4 npc_nodes=3 combat_ordinal=1 runtime_index=8`
  以及 `mock_npc_dialog ... direct_challenge=1`。这两个值分别证明没有再引用铁匠
  节点、并且没有走副本选择菜单。
