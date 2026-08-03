# CBE 如何处理「副本」相关战斗（2026-08-02）

CBE **没有**单独的「副本地图战斗」状态机。副本相关只有两套入口，最终都进
`mmBattle.cbm` 的同一开战解析器。

## 模块分工

| 二进制 | 地址 / 符号 | 职责 |
|--------|-------------|------|
| `江湖OL.CBE` | `net_handle_scene_channel_dispatch` `0x01039B8A` | `30/1` 进图；`30/9` 挑战确认文案分支 |
| `江湖OL.CBE` | `HandleChallengeResponse` `0x010395AA` | 解析 `30/9` → 确认框（**不是**开战） |
| `江湖OL.CBE` | `HandleResConfirmCb` `0x01039566` | 确认后发 `30/10{agree}` |
| `江湖OL.CBE` | `DispatchItemEvent` `0x01039C28` | kind-26 结束清进度层；需先 `26/0` |
| `江湖OL.CBE` | 业务回调闸 `0x01012F8E` | 错上下文时吞掉战斗模块回调 |
| `mmBattle*.cbm` | `HandleServerBattleCmd` `0x7BD0` | kind-4 分发；subtype 5 **与** 10 都进开战 |
| `mmBattle*.cbm` | `HandleBattleStartMsg` `0x66CC` | 解析 `battleinfo`，建左右阵营，切战斗屏 |

## 路径 A：场景怪（副本地图内踩怪 = 野外同形）

```text
mmGame 场景壳 + live-node 表
  → 碰撞发 1/4/1 { id, index, posx, posy [, moveinfo] }
  → 服务端 1/4/5 { side, battleinfo }   # scene_start=1
  → 0x7BD0 → 0x66CC
  → 用 index/pos 在 *(R9+13476) 25 格表抄怪（node+240/+244）
  → 进 mmBattle UI
```

`battleinfo`（subtype 5）只带 **场景索引与坐标**，不带完整怪模板；怪名/立绘/血蓝来自场景行。  
副本地图只要 SCE 刷了 kind=3 怪，客户端仍走这条，**不因场景名是 29* 而换解析器**。

## 路径 B：NPC 守关挑战（非场景开战）

```text
对话 action=1 → 26/1 {type=2,id=ECxxxxxx}
  → 服务端 26/0 + 30/9 {isleader,challenge}
  → 玩家确认 → 客户端 30/10{agree}
  → 主 CBMR: 30/10 ack
  → 同 TCP HAS_FOLLOWUP: lone 1/4/10 { side, battleinfo }  # scene_start=0
  → 0x7BD0 → 0x66CC
  → 从包内 vitals/name/visual 建怪（不扫 live-node）
  → 进 mmBattle UI
```

要点：

1. 向导 NPC **不是** live-node 怪，不能走 subtype-5。
2. `4/10` 必须挂在 **确认 data 请求的 followup** 上；挂 poll / 与 `30/10` 同包 / 与错误 `4/1` 同包都会被闸门或错上下文吃掉（`2026-07-30-instance-challenge-action13-no-battle.md`）。
3. 进副本传送是另一条：`30/1` → 场景资源 / ScreenInit；与开战无关。

## 路径对照

| | A 场景踩怪 | B NPC 守关 |
|--|-----------|------------|
| 请求 | `4/1` | `30/10`（前有 `30/9`） |
| 开战 WT | `4/5` | `4/10` |
| 怪来源 | 场景 live-node | 包内模板 |
| 依赖 map ctrl / 场景表 | **是** | **否**（仍需能切屏的壳） |
| 投递 | 可作 `4/1` 主响应 | 须 followup，且主包只能是 `30/10` ack |

## 进战之后

开战成功后两条路径共用 `mmBattle`：`4/2` 操作、`4/6` 回放、`4/8` 结算退出等。  
CBE 不再区分「这是副本来的还是野外来的」。

## 与梦境问题的关系

梦境踩怪发的是路径 A 的 `4/1`；热壳上 `4/5` / followup `4/10` 均未切入 mmBattle  
（见 `2026-08-02-dream-vs-outdoor-encounter.md`）。  
根因在 **场景壳能否完成 0x66CC 切屏**，不是 CBE 缺少「副本战斗」专用 opcode。
