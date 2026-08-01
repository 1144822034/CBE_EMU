# NPC 购药 pending 收尾遗漏（2026-07-30）

## 症状

多次向 NPC 购买药品后取数进度条不消失，直至网络超时。

## 契约回顾

见 `docs/re/2026-07-25-npc-purchase-progress-stuck.md`：

1. `shop-buy` 只回 `26/1`（清 busy）
2. poll phase1：`7/7`（可再置 busy）
3. poll phase2：lone `26/0`（再清 busy）

## 代码遗漏（根因候选）

| 缺口 | 位置 | 后果 |
|------|------|------|
| phase2 过期直接 `drop`、不发 `26/0` | `vm_net_mock_build_pending_npc_purchase_backpack_response` | `7/7` 已点亮 busy 后永久卡住 |
| 购药/仓库取回 peel 排在 `sceneVisibleReady` 门后 | `build_scene_sync_poll_response` | 场景未就绪时 poll 空返回，phase2 发不出 |
| 单槽 pending 覆盖无日志 | shop-buy / warehouse-retrieve 共槽 | 连买只 peel 最后一件；phase2 被冲回 phase1 |
| 非法 phase/字段 `return 0` 不清理 | 同上 builder | zombie pending 直到过期再无声丢弃 |
| 仓库存入 resync 同类 phase2 过期丢 ack | `build_pending_backpack_list_resync_response` | 同族 busy 卡死 |

## 修正

1. phase2 过期仍发 lone `26/0`（`reason=expired-phase2-still-ack`）。
2. NPC purchase + backpack list resync 挪到 `sceneVisibleReady` 检查之前。
3. `vm_net_mock_arm_npc_purchase_backpack_pending` 统一武装并打 `npc_purchase_backpack_rearm` 警告。
4. 非法 pending 立即 drop（尚未 peel 则 busy 仍由买时 `26/1` 清过）。

## 仍已知限制

单槽 pending：连续购买时较早序号的 `7/7` 可能永不投递（服务端背包已有货，客户端增量可能缺行，需重开背包/全量同步）。不在本次用队列扩大范围。

## 验证

```text
shop-buy ...
npc_purchase_backpack_deliver ...
npc_purchase_busy_ack ... reason=poll-phase2
```

异常路径可见 `reason=expired-phase2-still-ack` 或 `npc_purchase_backpack_rearm`。
