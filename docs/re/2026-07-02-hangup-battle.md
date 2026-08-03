# Hangup Battle Request

## Runtime Signature

- Request WT: `2/10`
- Objects:
  - `1/2/10` with `Type = 2`, payload length `10`
  - empty `1/25/3`
  - The live scene button may append one `1/2/1` object whose `moveinfo`
    field is the pending ten-byte direction timeline (object payload length
    `23`). This is a movement-queue flush in the same WT packet, not a second
    hangup marker.
- Observed failure before handling:
  - `unhandled wt=2/10 len=24 objects=1 first=1/2/10:10,1/25/3:0`
  - `unhandled wt=2/10 len=52 objects=1 first=1/2/10:10,1/25/3:0,1/2/1:23`

## IDA Evidence

- `JianghuOL.CBE:0x01015E14` (`HandleBattleEnterReq`) builds the outgoing `2/10` game event and writes `Type = 2`.
- `JianghuOL.CBE:0x01012E4D` dispatches business response subtype `25`.
- `JianghuOL.CBE:0x01010C7E` consumes response-side:
  - `25/11`: `result = 8`, then `info` string for the center banner state.
  - `25/12`: `result = 4`, then clears the banner state.
- No response-side `25/3` parser was found. The request marker must not be echoed.
- `JianghuOL.CBE:0x01012ADC` dispatches response `2/1`; its subtype-1 branch
  does not read fields. Therefore the appended movement upload is answered by
  the normal empty `2/1` acknowledgement.
- `mmBattle:0x66CC` consumes battle-start `4/5` with `side` and `battleinfo`.

## Server Contract

Success response:

- `1/2/10`: empty actor-other acknowledgement.
- `1/2/2`: the selected scene monster's HP/MP seed.
- `1/4/5`: scene-monster battle start. Its `battleinfo` contains the server
  SCE2 combat-spawn tuple and local player vitals; the client copies the
  monster model from its existing scene node.
- Optional `1/4/11`: auto-battle UI flag, controlled by
  `CBE_HANGUP_BATTLE_AUTO_FLAG`.
- When the request contains a trailing movement upload, one empty `1/2/1`
  acknowledgement follows the battle objects.

Failure response:

- `1/2/10`: empty actor-other acknowledgement.
- `1/25/11`: `result = 8`, `info = "No hangup monster"` or `"Monster not ready"`.
- When present, the trailing movement upload is still consumed through the
  existing movement handler and receives the same empty `1/2/1`
  acknowledgement. Its position/session side effects are not skipped merely
  because no battle target is ready.

## Data Source

- The server chooses the hangup monster from `automonster.dsh`.
- Load order:
  - `JHOnlineData/automonster.dsh`
  - `bin/JHOnlineData/automonster.dsh`
  - `web/fs/JHOnlineData/automonster.dsh`
- Matching uses loose scene-name comparison, then chooses one of the row monster ids.
- `CBE_HANGUP_BATTLE_ENEMY_ID` can force a monster id for debugging only.
- The service chooses the monster **type** from `automonster.dsh` and selects
  its first matching SCE2 combat spawn from the server-owned scene resource.
  `HandleBattleStartMsg(0x66CC)` resolves that source tuple by coordinate if
  its SCE ordinal differs from the client's live node slot.

## Implementation Notes

- Handler source: `src/mock-server.c`, `builtin-hangup-battle-start`.
- This is intentionally narrower than generic `2/10`: it requires the exact
  `Type = 2` plus empty `25/3` signature, followed by either no object or
  exactly one valid ten-direction `2/1 moveinfo` upload. Other trailing objects
  remain unhandled.
- Do not add JSON fallback or client-global reads for this feature. The server must answer from server-side scene and `automonster.dsh` data.

## 2026-07-20 Regression

- First crash fix: replaying the exact 52-byte three-object request returned a
  bounded failure response instead of reaching the unhandled assertion.
- Follow-up stall evidence: the real 24-byte button request returned
  `2/10 + 25/11 "Monster not ready"`. `HandleBattleEnterReq(0x01015E14)` had
  already set the client battle state to `3`, while the banner parser did not
  reset it, leaving the UI at `获取数据`.

### 2026-07-28 visual-contract correction

The temporary `4/10` path avoided the old null scene-node draw, but user
runtime then showed a player-shaped opponent on the left and a player attacking
that opponent. This is an earlier protocol deviation, not a rendering bug.

`mmBattle:HandleBattleStartMsg(0x66CC)` gives subtype `4/10` one full left row
with two visual bytes and a short right row resolved from the local party
template. The same full-row construction is used by the verified duel builder,
where those two bytes are the peer's job and sex codes. They are not an
arbitrary `.actor` resource field. The hangup builder supplied generic `0/1`,
which the client faithfully decoded as a player appearance; `4/10` therefore
cannot represent an `e_mucusP.actor` monster.

The server/source resource contract is now sufficient for a real scene start:

- `bin/JHOnlineData/01桃花岛_01.sce` and
  `web/fs/JHOnlineData/01桃花岛_01.sce` share SHA-256
  `AE11796E2970FF97A5CBBC855F7E1141A75E8D96E79B16DC9A2CDF402863FCEE`.
- Decoding that common SCE2 resource produces four `actor_id=105` combat
  spawns: `(295,57)`, `(179,120)`, `(146,349)`, `(292,484)`; each has
  `e_mucusP.actor`.
- In the same runtime, normal collision battles successfully sent live tuples
  `(index=6,pos=(295,57))` and `(index=8,pos=(146,349))`. Thus the source
  resource's coordinates are present in the live client node table.
- `0x66CC` first tests its supplied index then scans active kind-2 nodes by
  `node+240/+244` coordinates. The SCE ordinal need not equal the live slot.
- `scene_node_update_move_blob(0x01012A76)` seeds HP/MP at the first active
  actor-id match. The hangup selector also chooses the first matching SCE
  combat spawn, so its preceding `2/2` and the `4/5` source refer to the same
  monster node.

The corrected response is:

```text
1/2/10 { othernum=0, otherinfo="" }
1/2/2  { moveinfo for first verified SCE combat spawn }
1/4/5  { side=1, battleinfo(scene-index, scene-x, scene-y, player vitals) }
1/4/11 { result=1, type=1 }                 # if auto is enabled
[1/2/1 empty acknowledgement]               # only when uploaded with request
```

There is no `4/10` fallback for scene hangup. If the selected
`automonster.dsh` id has no corresponding server SCE2 combat spawn, the handler
returns `2/10 + 25/11` instead of fabricating a player-template battle.

Expected trace:

```text
mock_hangup_battle_start source=request ... subtype=5 index=<sce ordinal> pos=(<x>,<y>)
  target_source=sce-combat-spawn-coordinate
  ... response=2/10+2/2+4/5[+4/11][+2/1]
```

Manual regression required:

1. On 桃花岛_01, press 挂机 twice. Both entries show 毒泥怪 on the left and
   the local role on the right; no player-shaped opponent appears.
2. Automatic solo actions use scene slots (`actor=1,target=0`) and the result
   panel returns to the scene normally.
3. Repeat within the eight-second reward cooldown. The visual contract remains
   subtype 5, while terminal closure follows the separate no-reward
   `4/11 + 4/9` poll contract.
4. A configured hangup monster with no server SCE2 spawn shows the bounded
   `Monster scene node unavailable` banner rather than entering battle.

## 2026-07-28: 跨战斗挂机续接

### 触发条件与实际偏离

原先服务端把一次 `2/10(Type=2) + 25/3` 只当作“开一场自动战斗”的
请求。战斗结束后的日志会出现新的 `mock_hangup_battle_start`，但每一次都
来自玩家再次点击挂机，两个战斗之间没有服务端自行发起的 `4/5`。

这不是 `4/11` 自动攻击缺失：`4/11(type=1)` 只控制**当前** Battle 场景中的
自动回合，终局会由战斗 builder 清掉该单场状态。客户端真实的收尾顺序是：

```text
4/6（最后动作） -> [4/7（有奖励时）] -> 4/11(type=0) + 4/9
  -> 客户端完成结果动画并关闭战斗画面 -> 空 25/5
```

`Battle.cbm` 的 `4/9` 分支负责最终的战斗画面收束；随后才由场景运行时发出
空 `25/5`。旧服务端把这个 `25/5` 统一回复为普通 `25/5(result=4)`，没有保存
“该战斗是挂机轮次”的会话状态，也没有在后续场景轮询投递下一场。因此首次偏离
发生在**已安全回到场景后的续接丢失**，而不是客户端自动战斗或渲染。

### 修复契约

- `vm_mock_service_client_session` 新增仅在线有效的 `sceneHangup*` 状态；它不写
  角色数据库，也不会跨掉线、账号接管或服务重启残留。
- 初次 `2/10(Type=2)+25/3` 成功建立经 SCE2 坐标验证的 `4/5` 后，记录角色、场景
  与 battle serial。
- 仅当同一 serial 的胜利终局已收到客户端空 `25/5` 时，才将下一场排入**至少
  1000 ms 后**的 scene-sync poll。续接响应严格为：

  ```text
  1/2/2  场景怪物 HP/MP seed
  1/4/5  同一 SCE2 怪物节点的 battleinfo
  1/4/11 { result=1, type=1 }
  ```

  它不是伪造或重放 `2/10`，也不会与结算 `25/5` 同包发送。
- 续接前重新验证会话仍在原场景、角色与 battle serial 一致；场景切换、掉线、
  账号接管、死亡、非胜利终局或找不到已验证的场景怪物都会明确停止挂机，而不会
  把旧场景的节点带入新场景。
- 客户端已证实的显式取消操作是战斗内 `4/11(type=0)`（取消自动）。若当前战斗
  属于场景挂机，服务端同时清理 `sceneHangup*`，当前场战斗回到手动操作，结束后
  不再续接。没有为场景按钮猜测一个不存在的“退出包”。

### 可观测日志与人工复测

预期顺序：

```text
scene_hangup_start ... source=request
mock_hangup_battle_start source=request ... response=2/10+2/2+4/5+4/11
... 最后一份 4/6 / 4/7 / 4/11+4/9 ...
scene_hangup_round_complete ... evidence=4/9->25/5->poll
scene_hangup_start ... source=scene-poll
mock_hangup_battle_start source=scene-poll ... response=2/2+4/5+4/11
```

人工复测：在有 `automonster.dsh` 和对应 SCE2 怪物节点的野外场景开启挂机，连续完成
至少两场；第二场应由 scene poll 自动进入。第二场中点击客户端“取消自动”，日志应出现
`mock_battle_auto_toggle type=0` 与 `scene_hangup_stop ... reason=battle-auto-cancel`；本场
结束后不得再出现 `source=scene-poll` 的新开战。随后切图、重新登录和角色死亡也应不产生
遗留续接。

### 2026-08-01 终局关闭与怪物数量修正

该文先前把挂机胜利的末尾简写为 `4/11(type=0)+4/9`，但这不能用于有奖励的
内联 `4/7` 结算：`0x7BD0` 的 case 11 会先清掉 case 9 进入自动终局所需的
`battle+1140` 标记，客户端便停在结果页，只有用户点击后才会发送空 `25/5`。

当前仅当请求所属角色、battle serial 与 `sceneHangup*` 会话严格一致时，最终
`4/6 -> 4/7 [-> 7/7]` 保持在同一响应；`4/9 { result=1 }` 则留到最终动作队列跨过
客户端播放边界后的下一次 scene poll。这样既保留开始时的自动标记使客户端走
`0x7BD0 -> 0x5444` 的原生关闭路径，也不会在 type-3 最终死亡回调前切换终局阶段。
无奖励终局不生成 `4/7`，但同样只在该边界后发送 `4/9`。普通战斗、切磋、队伍与复活的
终局对象不因此改变。

同一次排查还确认挂机 builder 曾将 `battleEnemyCount` 硬编码为 `1`；它现在复用普通
场景怪物战斗已验证的 `vm_net_mock_battle_roll_enemy_count(true)`，默认生成 1–3 名
同模型场景怪物。详见
[`2026-08-01-scene-hangup-control-and-loop.md`](2026-08-01-scene-hangup-control-and-loop.md)。
