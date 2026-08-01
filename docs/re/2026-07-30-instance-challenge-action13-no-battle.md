# 副本挑战确认后未进战（2026-07-30）

## 症状

临安 `炼狱魔头`（actor=30461，`npc_kind=6`）点「挑战守关怪」后，服务端已回开战包，客户端仍停在地图。

## 阶段 1：action13 同包 4/10

菜单曾用 task-hall action=13 → 客户端发 `4/1`，同包回 `4/10`。`0x01012F8E` 业务回调门住战斗模块回调。

修正：挑战选项改为 `action=1` + `CHALLENGE_INSTANCE_BASE|actorId`（EC），走 `26/0+30/9` → `30/10`。

## 阶段 2：`objects=2`（25/12+4/10）

EC 时序对齐后仍不进战；开战包前置了全局 `25/12` 清 banner。

修正：`forceNonSceneStart` 时不前置 `25/12`，只发纯 `4/10`。

复测日志：`challenge_battle_start ... objects=1`，`resp=116`，客户端 `queue_scene_poll ... resp=116`，仍不进战。

## 阶段 3：scene poll 投递落错请求上下文（当前根因）

### 证据

```text
challenge_confirm ... battle_delivery=next-scene-poll resp=23
challenge_battle_start ... objects=1
instance_challenge_battle_deliver ... response=4/10 resp=116
scene_sync_poll ... response=116

客户端:
queue_data ... resp=23          # 30/10 ack
queue_scene_poll ... resp=116   # 4/10 挂在场景同步应答上
# 无 mmBattle / 战斗切屏；随后空 poll
```

首次偏离：把应在 **确认 data_request** 上投递的 `4/10`，改成了 **scene_sync_poll** 应答。poll 应答场景同步 outstanding 请求；kind-4 到不了开战路径。同包 `30/10+4/10` 也不行（业务门），但仓库 834 / 售卖 839 已证明的契约是：**同一 data TCP 上 HAS_FOLLOWUP 第二帧 event=7**。

### 修改

1. `30/10` 确认仍只回 `30/10{result=0}`，并 arm `instanceChallengeBattlePending`。
2. `mock_server_transport` 在同请求上 `take` 开战包为第二 CBMR（`HAS_FOLLOWUP`），日志 `mock_npc_instance_challenge_battle_wire`。
3. scene poll 仅作 `age_ticks>=2` 的兜底，正常路径不应再走到。

### 复测

1. 重启服务；点炼狱魔头 → 挑战守关怪 → 确认。
2. 日志：`challenge_confirm ... battle_delivery=data-followup` → `challenge_battle_wire ... resp=116`（或同类长度）→ `challenge_battle_start ... objects=1`。
3. 客户端应出现 `queue_data_followup` / 连续两次 data 投递，并进入战斗 UI；不应再依赖 `queue_scene_poll resp=116` 开战。

## 阶段 4：followup 开战后 `no-armed-session`（2026-07-30）

### 证据（蓬莱 `小猴子`）

```text
challenge_confirm ... battle_delivery=data-followup
challenge_battle_wire ... followup=116 flags=2
builtin-actor-other-only10 resp=40
mock_battle_operate_ignore reason=no-armed-session awaiting_settle=0
```

客户端已进战斗并发 `4/2`，服务端却无武装会话。

### 根因

`mock_server_transport` 先 `account_capture`（此时 `armed=0`），再清 active context，最后才 `take` 挑战 `4/10` followup（把全局 `armed` 写成 1）。下一请求 `account_restore` 用旧快照把 `armed` 冲回 0。

临安 `炼狱魔头` 同包 followup 已发出但未进战，与此独立；蓬莱能进战却卡操作是本阶段契约破坏。

### 修改

HAS_FOLLOWUP 的 take 挪到 `account_capture` 之前，且仍在 `active_client_id` / account restore 有效期间，使开战武装写入账号快照。

## 阶段 5：临安仍忽略 followup `4/10`；subtype-10 立绘女天机（2026-07-30）

### 证据

临安炼狱魔头确认后：

```text
challenge_confirm ... battle_delivery=data-followup
challenge_battle_wire ... resp=116 flags=2 followup=116
客户端 queue_data resp=23 + queue_data_followup resp=116
# 仍无 mmBattle / 4/2；随后空 poll
```

同拍 HAS_FOLLOWUP 在蓬莱可进战、临安忽略。把 `4/10` 当作 scene_poll **主**应答也不进战。

立绘：`challenge_enemy_id` 开战包左侧曾默认 visual `0/1`（女天机）。后台/用户意图是
绑定正确怪物 Actor；但 **subtype-10 的 name 字段不能写 `.actor` 文件名**（阶段 7 闪退）。
场景 npcinfo 第四串才是 `.actor` 键。

### 修改

1. ~~确认 `30/10` 只清 pending；`4/10` **延迟 ≥1 scheduler tick**~~ — **已回退**
   （阶段 6：该延迟破坏蓬莱同拍进战契约）。
2. 废除 poll 主包替换为 `4/10` 的兜底（保留）。
3. `challengeX/Y==0` 不再丢开战包（subtype-10 不用 live-node 坐标；回退 wire 1,1）。
4. subtype-10：**name=显示名**；**visual 默认 `0/1`**（防崩；女天机肖像）。
   `0/0` 空肖像闪退；SCE field-16→男鬼道。怪物真立绘：`unresolved`。见阶段 8。

临安同拍 followup 被忽略：阶段 9 用「同拍保留 pending + age≥1 二次 HAS_FOLLOWUP」
补投递；不得再改成 lone `30/10`。

### 复测

1. 蓬莱：`battle_delivery=data-followup` + followup `4/10` → 进 mmBattle；
   `mock_battle_start_info ... visual=0/1`（不闪退；立绘可能仍是女天机）。
2. 临安：同拍 followup 若仍忽略，应在下一拍见 `retry=1` 二次 wire（阶段 9）；勿再改成 lone `30/10`。
3. 勿用 visual `0/0` / field-16；勿把 `e_boar.actor` 写入开战 name。

## 阶段 6：延迟 followup 导致小猴子无法进战（2026-07-30）

### 证据

```text
challenge_confirm ... battle_delivery=deferred-data-followup resp=23
net_send ... wt=30/10 ... resp=23
account=... response=23 event=7 flags=0 followup=0
# 无 challenge_battle_wire / 4/10；客户端停在地图
```

对比阶段 4 蓬莱成功路径：`battle_delivery=data-followup` + `flags=2 followup=116`。

### 根因

首次偏离：`take_instance_challenge_battle_wire_followup` 要求 `age_ticks>=1`，
确认同拍故意不挂 `4/10`。蓬莱客户端在 lone `30/10` 后不进 mmBattle（阶段 4/仓库
同拍第二 CBMR 契约）。延迟是为临安做的未验证兜底，违反「不得破坏已证明路径」。

### 修改

去掉 `age_ticks<1` 门闩，恢复确认同拍 HAS_FOLLOWUP；仍禁止用 poll 主包投递 `4/10`。

## 阶段 7：subtype-10 左侧写 `.actor` 文件名导致闪退（2026-07-30）

### 证据

```text
mock_battle_start_info ... name=e_boar.actor visual=0/0 actor=e_boar.actor
challenge_battle_wire ... followup=121
客户端 queue_data resp=23 + queue_data_followup resp=121
地址无法访问:8 ... pc:1004e1c
r4 单元内已有 ASCII "e_boar.actor"
```

### 根因

误把场景 `npcinfo` 第四串（必须 `.actor`）套用到 battleinfo subtype-10 的 **name**。
客户端解析 name 后按战斗立绘路径取对象失败 → 空指针 +8。

### 修改

左侧 name 改回 **显示名**；visual **不要**用 SCE field-16。目录
`actor_resource` 仍保存，仅作配置/日志。见
`2026-07-30-monster-admin-actor-resource.md`。

## 阶段 8：SCE field-16 → 男鬼道（2026-07-30）

### 证据

开战 `visual=5/0`（野猪 SCE field-16）后客户端左侧显示**男鬼道**。

### 根因

subtype-10 的 `visual_group/variant` 经 `sub_23F6` 映射到玩家职业肖像
（`GetMapTileData(jobIndex, sexGroup)`），与 SCE 战斗行 field-16 无关。

### 修改

PvE subtype-10 visual：**不可用 `0/0`**（空肖像 → 同址闪退）。默认恢复历史
`0/1` 保进战。field-16 仍禁用。怪物 `.actor` 立绘标 `unresolved`。

### 补充证据（name=野猪 visual=0/0）

```text
mock_battle_start_info ... name=野猪 visual=0/0
queue_data_followup resp=113
地址无法访问:8 pc:1004e1c
r4 单元名=野猪(GBK)
```

与 name=`e_boar.actor` 同崩溃点：根因是空立绘对象，不是单独「名字写了 .actor」。
   （如 300 暂无 SCE actor）会记 display 回退，立绘仍可能 unresolved。

## 阶段 9：临安同拍忽略后的 poll 二次 HAS_FOLLOWUP（2026-07-30）

### 证据

```text
# 临安炼狱魔头
challenge_confirm ... battle_delivery=data-followup
challenge_battle_wire ... age_ticks=0 resp=113
queue_data_followup resp=113
# 无 2/10 / 4/2 / mmBattle；随后空 poll resp=0
# take 后曾立刻清 instanceChallengeBattlePending → 无第二次投递
```

蓬莱同路径会进战并发 `2/10`；不可改回「确认只回 lone 30/10」（阶段 6 负例）。

### 根因

首次偏离在**客户端忽略确认同拍的 `4/10`**。服务端在首次 `take` 后清掉 pending，
没有把 `4/10` 再挂到后续 event=7 的机会。

### 修改

1. 同拍仍投递 `4/10`（保蓬莱），但保留 `instanceChallengeBattlePending`，
   记录 `instanceChallengeBattleWireCount`。
2. `age_ticks>=1` 且 pending 仍在：允许再 `take` 一次（可挂在空 poll 主包 +
   HAS_FOLLOWUP；**禁止**把 poll 主应答换成 `4/10`）。
3. 客户端进战后清 pending：`2/10` actor-other / `4/2` battle operate
   → 蓬莱不会二次 `challenge_battle_wire`。
4. 挑战开战清 `g_mockBattleAutoPrefer` / `g_mockBattleLastOperateValid`
   （避免上一场技能态污染）。
5. 立绘边界不变：subtype-10 `name=显示名` + `visual=0/1`；目录 Actor 仍
   `unresolved`（见 `2026-07-30-monster-admin-actor-resource.md`）。

### 复测

1. 蓬莱小猴子：同拍 `flags=2 followup=*` → `2/10`/`4/2`；**不应**出现
   `challenge_battle_wire ... retry=1`。
2. 临安炼狱魔头：同拍仍发一次；若忽略，下一拍应见
   `challenge_battle_wire age_ticks>=1 wire_count=2 retry=1` + HAS_FOLLOWUP，
   并进入 mmBattle。
3. 立绘本轮仍允许女天机；不以「看起来像怪」为完成标准。
