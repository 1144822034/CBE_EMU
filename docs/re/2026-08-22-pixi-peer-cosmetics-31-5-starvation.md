# Pixi 附近翅膀不显示：31/5 投递饥饿 / 空包忽略

Date: 2026-08-22

## 现象

活死人墓等多人图：自己翅膀可见，附近玩家无翅/骑/称号。取证 JSON：

- `peers: 17`，`peersWithCos: 0`，`pendingCos: 0`
- `plist.cachedSheets: 1`（仅自己）
- 长时间 `RENDER_STALL` + `brokenTex` 全是 `_Graphics`（误报）

结论：不是 plist 画不出来，而是 **marker 上从未写入非零 eqWing/eqMount/eqChenghao**（客户端未收到有效 `1/31/5`，或只收到全 0 且被忽略）。

## 根因（服务端契约）

1. **空 31/5 被客户端永久忽略**  
   `GameSession` / `ActorLayer`：第一次全 0 的 peer 31/5 直接丢弃且不建 pending。  
   `enqueue_peer_cosmetics` 曾只用 `source->onlineEq*`；若 baseline/moveinfo 早于 `capture_online_presence` 同步，会入队并 flush 全 0 → 之后即使角色库有翅膀也不再重发（直到换装广播）。

2. **scene-sync 对象预算饿死 31/5**  
   `VM_NET_MOCK_MAIN_BUSINESS_OBJECT_MAX=10`。附近 baseline ≈ `1 + N` 个 moveinfo，N≥10 时 `budgeted` capacity=0，化妆品只挂在 chat 路径尾部，整包不 flush。  
   另：chat 先于 31/5 消费剩余槽位，忙图进一步饿死。

3. **idle 快路径未看 cosmetic 队列**  
   无 chat/社交时可能直接 return 0，pending 31/5 留在队列（与有人图叠加时次要）。

`RENDER_STALL`：ticker 走 `renderer.render`，旧取证只包了 `app.render`，`lastOkRenderAt` 不更新 → 假 stall。Graphics 无 `texture.source` 属正常，不应进 `brokenTex`。

## 修改

| 层 | 改动 |
|----|------|
| `mock_server_cosmetics.c` | enqueue 时从 role DB 回填 equipped；全 0 不入队；`has_pending_cosmetics` |
| `mock_server_social.c` | 31/5 先于 chat；budgeted 对 pending 化妆品允许最多 +8 溢出；idle 不跳过 pending 化妆品 |
| `main.ts` / `glForensic.ts` | 包 `renderer.render`；扫描跳过 Graphics |

## 验证

1. 重启 mock-server（本改动在服务端）。
2. 两人同图且对方已装备翅膀：控制台应有 `[session] cosmetics 31/5 peer=… wing=…` 与 `[actors] cosmetics paint`。
3. `__pixiDebugPeerCosmetics()`：`peersWithCos > 0`，`plist.cachedSheets` 随附近翅增加。
4. 取证不应再因单纯 ticker 渲染刷出持续 10s+ `RENDER_STALL`（除非真卡帧）。

## unresolved

- 附近玩家若未解锁/未装备 Pixi 化妆品，仍应无翅（预期）。
- CBE 原生客户端是否消费 31/5：仍为 Pixi 扩展对象；勿写入 classic titleBadge。
