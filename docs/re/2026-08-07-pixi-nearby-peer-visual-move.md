# 2026-08-07 Pixi 附近玩家外形错位与移动不刷新

phase: pixi nearby-peer visual + 1/2/2 move consume
status: fixed-in-client

## Symptoms

- 女幻剑显示成女鬼道；女鬼道显示成男鬼道。
- 其他玩家已移动，本端地图标记不跟着走。

## Root Cause

### Visual (job/sex)

Mock / CBE `ParseSceneOtherNodeData` wire contract（见 `docs/re/2026-07-07-nearby-players.md`）:

```text
visualVariant = job - 1   // 0=天机 1=幻剑 2=鬼道
visualGroup   = sex + 1   // 1=男 2=女
```

Portrait index evidence (`scene_runtime_init_and_sync`):

```text
index = visualGroup + 2*visualVariant - 1  ≡  job*2 + sex
```

Pixi `ActorLayer.applyPeers` 曾写成:

```text
job = visualGroup % 3
sex = visualVariant & 1
```

字段对调后：

| 真实 | wire | 错误解码 | 显示 |
|------|------|----------|------|
| 女幻剑 | vV=1, vG=2 | job=2, sex=1 | 女鬼道 |
| 女鬼道 | vV=2, vG=2 | job=2, sex=0 | 男鬼道 |

### Movement

Scene-sync 下行对已可见 peer 发 `1/2/2 { moveinfo = startX, startY, actorId, len16 dirs }`（见 `docs/re/2026-07-10-nearby-player-movement-sync.md`）。Pixi 只消费了 `2/10 otherinfo` / `2/6 actorid`，未把 timeline 推进到 `ActorLayer`，因此位置停在首次 baseline。

## Fix

- `ActorLayer.applyPeers`: `job = visualVariant`，`sex = visualGroup - 1`。
- 新增 `parsePeerMoveUpdates`：识别 `blobLen<=10` 且字节 `0..4` 的 timeline（或空 blob 定点），跳过怪物 vitals blob。
- `GameSession` → `onPeerMoves` → `ActorLayer.applyPeerMoves`：从 start 按 100ms/步、4px/方向消费队列，并播 walk 姿态。

## Validation

- 对照表与用户报告一致，无需改服务端序列化。
- 复测：双端同图，A 移动后 B 的 peer 标记应沿 timeline 前进并停在权威终点；换性别职业角色后外形与本端角色一致。

## Negative

- 不要用 `visualGroup % 3` 当职业。
- 不要把 `blobLen>=44` 的 monster moveinfo 当 peer timeline 重放。
