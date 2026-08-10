# 2026-08-07：切磋发起方不进战 / 被切磋方已进战

phase: spar-inviter-inline-4-10-object-count
status: fixed

## 症状

A 对空闲 B 发起切磋（周围玩家 → 切磋）：

- B（被切磋方）经 scene-sync poll 收到 `4/10`，进入战斗
- A（发起方）无进战 / 仅见 tip「切磋开始」或完全无反应

## 契约（预期）

见 `docs/re/2026-08-07-pixi-social-trade-spar-guild.md`：

1. `duel_begin` → `startPendingMask=3`
2. 邀请方同包：`4/14 result=1` + 内联 `4/10`
3. 对端：`startPendingMask` 留给下次 poll

## 第一次偏离

`vm_net_mock_build_nearby_spar_request_response` 在已写入 `4/14` 后，把
`build_duel_start_response` 的对象 append 进同一 WT，但 object count 写成：

```c
out[4] = (u8)(out[4] + startCount);
vm_net_mock_finish_wt_packet(out, pos, out[4]);
```

此时 `out[4]` **尚未**被本包初始化（回收缓冲残留）。常见残留 `0`：

- finish 后 `objectCount=1`
- Pixi `decodeWtPacket` 只解析第一个对象 `4/14`
- 内联 `4/10` 被静默丢弃
- `build_duel_start_response` 已清掉邀请方 `startPendingMask` bit → poll 不会再投递
- 对端 bit 仍在 → poll 进战

若残留 count≥2，可能误解析成功（偶发正常）；若过大则 decode throw，整包丢弃。

## 根因

邀请方同包 object count 未按「1 个 ack + startCount」书写，依赖回收缓冲残留；
与「双方都应进战」契约在邀请方 parser 入口第一次偏离。

## 修改

| 层 | 改动 |
|----|------|
| `mock_server_guild.c` | `finish_wt_packet(..., 1 + startCount)`；append 失败时恢复 inviter pending |
| `GameSession.sparChallenge` | 若仍未 `inBattle` 则 `pollOnce`（对齐 `confirmChallenge`） |

## 验证

- `make -j2`
- 双端同图：A 切磋空闲 B → **双方**进战；日志 `start_inline=1`，邀请方响应 objectCount≥2
- B 忙碌 → tip「对方忙碌」，无人进战

## Negative

- 不要用客户端硬切 `showPhase("battle")` 掩盖缺 `4/10`
- 不要在 append 失败且已 clear pending 时假装「仍 pending」却不恢复 mask
