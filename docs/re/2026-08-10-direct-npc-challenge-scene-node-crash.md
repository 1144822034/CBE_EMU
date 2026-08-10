# NPC 直接挑战误用场景战斗节点导致崩溃

## 触发与原始证据

在 `00蓬莱仙岛_02.sce` 对“小猴子”NPC 选择 action13 直接挑战后，客户端收到
`resp=185`，随后崩溃：

```text
queue_data connect=2 event=7 resp=185
地址无法访问:c ...
PC=01004EA8, LR=05032435, lastPc=05032432
```

服务端同一事务记录为：

```text
mock_scene_monster_target ... actor=1000 runtime_index=8 pos=(120,120)
mock_direct_challenge_scene_target ... action=action13-to-4/5
mock_challenge_battle_start ... subtype=5 index=8 req_index=0 req_pos=(0,0)
```

## 业务链路与首个偏离

```text
NPC action13
  -> 客户端 WT 1/4/1 { id=1000, index=0, posx=0, posy=0 }
  -> 服务端从部署后的 SCE 推算 index=8
  -> WT 1/2/2 + WT 1/4/5
  -> mmBattle:HandleBattleStartMsg(0x66CC)
  -> JianghuOL.CBE:0x01004EA8 空视觉上下文
```

`4/5` 的场景元组必须代表客户端**已经创建**的 type-2 节点。action13 会携带它按
怪物 ID 查得的 `index`，但 x/y 固定为零；本次请求的零 index 明确说明它没有在客户端
当前节点表找到目标。另一方面，本次服务端日志只出现了
具名资源发布和启动 `18/9`，没有 `00蓬莱仙岛_02.sce` 的 `18/7 clientmiss` 请求。
因此客户端保留旧同名缓存，服务端部署后的 kind-3 小猴子并未成为客户端 row 8。

首个契约违例发生在服务端忽略 action13 的零 index，改用服务器离线推算节点下发 `4/5`，
而不是最终的 `PC=0`/`0x01004EA8` 绘制症状。

## 后续固件复核（2026-08-10）

此前把 action13 一概归为 `4/10` 是错误的分类。重新检查
`江湖OL.CBE:task_hall_activate_selected_entry(0x010492B0)` 与
`SendNPCInteractReq(0x01037ED4)` 后，确认 action13 发送的 `1/4/1` 并非只有
怪物 ID：客户端以该 ID 扫描当前 25 个场景节点，并把命中节点写入 `index`；只把
`posx/posy` 写成零。

`mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` 给出了这两个下行类型的
互斥语义：

- `4/5` 消费 scene index 和服务器给出的静态场景坐标，定位 active kind-2 节点，
  从该节点复制名称、HP/MP 和 Actor 视觉资源；这是场景“小猴子”等挑战目标的正确
  下行类型。
- `4/10` 不读取任何场景节点，直接读取完整角色行末尾两个视觉字节并调用
  `sub_23F6`。它只能生成职业/性别角色模板，因而必然显示成玩家，而不是
  `e_monkey.actor`。

因此原始崩溃的首个协议错误不是 `4/5` 本身，而是服务端在**客户端没有该小猴子
live node** 的前提下，离线推算 row 8 并下发 `4/5`。本次请求的 `index=0` 是其
直接证据：`SendNPCInteractReq` 未在客户端当前表找到配置的战斗怪 ID。

正确修复应为：

1. action13 的 value 必须是已部署 kind-3 场景战斗怪的 monster ID，不能是普通
   NPC actor ID；
2. 仅当 action13 上行的 nonzero `index` 与当前已验证 SCE2 战斗怪记录相符时，
   服务端以该客户端 index、该静态 spawn 的 x/y 返回 `1/2/2 + 1/4/5`；
3. 若客户端尚未装载该 SCE（或 index 不匹配），拒绝本次挑战并提示资源/场景未就绪；
   不得降级为 `4/10`，也不得离线猜一个场景 row。

客户端已有同名 SCE 的版本更新/失效仍是第 2 步能够成功的前提；WT18/7 cache miss
不能替代这项证明。

## 修复（2026-08-10）

服务端现将 action13 的两种客户端契约分开处理：

- 当前场景 kind-3 怪物的直接挑战会标记为 `instanceChallengeDirectSceneMonster`。
  服务端要求 `1/4/1` 同时匹配敌人 ID、当前可见场景、非零请求 `index`、已实际下发的
  27/11 NPC 节点计数，以及 SCE2 解析出的精确运行 index；满足后才用该 index 与 SCE2
  的 x/y 下发 `1/2/2 + 1/4/5`。
- 配有目标场景的副本挑战没有当前场景怪物节点，保留独立的 `1/4/10` 开始路径。
- 场景挑战的任一前置不成立时，返回 `2/10 + 25/11`，提示“挑战目标尚未加载，请重新进入
  场景后重试”。它不会再落入通用挑战 builder，因此既不会猜测离线 row 下发 `4/5`，也不会
  用玩家模板 `4/10` 掩盖场景资源未就绪。

回归必须同时确认：上行 action13 `index` 非零且匹配、响应为
`1/2/2 + 1/4/5`、左侧显示 `e_monkey.actor`，并且不出现 `PC=0` 或
`0x01004EA8`。缓存旧 SCE 的客户端必须得到显式未就绪结果，而不是开始错误战斗。

## 提示文本编码复核（2026-08-10）

未就绪分支的 `25/11.info` 是客户端按 GBK 读取的零结尾字符串。首次实现时手工抄写
“挑战目标尚未加载，请重新进入场景后重试。”的转义字节，在“请重新进入”之间多写了一个
`0xBD`。这会使后续 GBK 双字节边界错位，客户端虽然正确消费了 `25/11` 对象，却将后半段
渲染为随机字符。

修复只替换该 `info` 字段为逐组校验过的 GBK 字节序列；`2/10 + 25/11` 的对象类型、字段
顺序和拒绝语义均未改变。回归时应验证提示完整显示为“挑战目标尚未加载，请重新进入场景后重试。”。
