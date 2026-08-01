# 退出商城路径矩阵

Date: 2026-07-28

Status: implemented (server) — authoritative contract for mmShop exit follow-up

```text
trigger: mmShop exit on empty / NPC scenes (incl. after map-stone / login)
falsified: server already-seeded ≠ client still shows type-21 after mmShop
contract: NPC maps always re-poll nonempty 27/11 after shell 30/2
```

## 三条契约（互不干涉）

| 场景 | shell 30/2 | 再发 27/11 | kind-2 30/1 | 原因 |
|------|------------|------------|-------------|------|
| 空图 `npcnum=0` | 是 | 否 | 是（每店一次） | 无 type-21，靠 30/1 重建怪/碰撞 |
| NPC 图 | 是 | **是**（shell 后 poll） | **否** | mmShop 清 type-21，靠 27/11；不再二次 30/1（体验） |

```text
mmShop exit
  → npcnum==0:        shell 30/2 → kind-2 30/1
  → npcnum>0:         shell 30/2 → poll 27/11 → post-catalog 30/2 → busy（无 kind-2）
```

## 已证伪假设（2026-07-28 蓬莱登录进店）

曾假设「服务端 `g_vm_net_mock_scene_moveinfo_npc_seeded` 则客户端 type-21 仍在，可 skip 27/11」。

用户日志：

1. 登录已 `startup-scene-followup-immediate` seed `npcnum=3`
2. 出店 `shop_return_npc_catalog_skip reason=already-seeded`
3. shell / moveinfo 正常可走
4. **NPC 不显示**

结论：开店后客户端 NPC 视图已空；服务端 seeded 只表示「曾投过目录」，不能代替出店再发 `27/11`。

## 修改点

1. `shopReturnSeed` + `npcnum>0`：始终 empty gate + `shopReturnNpcCatalogPending`（`catalog_defer`）；**无** `catalog_skip already-seeded`。
2. shell `remaining=0`：有 catalog → ready；post-catalog 完 → busy + kind2；无 catalog 且 `npcnum==0` → kind2。
3. `moveinfo-live`：有 catalog → ready；取消的是 **post-catalog** clear → kind2；仅空图 shell cancel → kind2。

既有保护保持：

- `finish_shop_return_rehydrate` 消费 `shopSceneNpcReseedPending`
- `shopReturnKind2Completed`（每店至多一次 kind-2）

## 与相邻文档的边界

| 文档 | 边界 |
|------|------|
| `2026-07-28-shop-return-npc-catalog-defer.md` | 权威 defer→poll 顺序；本矩阵确认 **所有** NPC 出店都走这条。 |
| `2026-07-28-penglai-shop-return-after-teleport-stall.md` | 地图石后二次 27/11 曾卡住；根因不是「不该再发」，而是投递/clear 时序。出店仍必须补目录。暖目录短窗见 `2026-07-28-shop-return-post-catalog-lite.md`。 |
| `2026-07-28-shop-return-post-catalog-lite.md` | priorSeeded → remaining=3 + 可选 2s settle；冷目录仍 remaining=8。 |
| `2026-07-28-penglai02-shop-return-kind2-portal-snap.md` | NPC 出店仍 kind-2；禁止 follow-up 2/3 用 SCE exit-0 甩到出口。 |
| `2026-07-28-shop-return-menu-dismiss-late-kind2.md` | post-catalog + moveinfo 立即 cancel+kind2，避免菜单被 30/2 顶掉与晚二次进图。 |

## 验证

```text
# 蓬莱（登录或地图石后）进/出商城
shop_return_npc_catalog_defer ... npcnum=3
shop_return_npc_catalog_ready ... via=shell-clear-done|moveinfo-live
mock_shop_return_npc_catalog_deliver ... post_catalog=1
shop_return_kind2_reenter_arm ... via=post-catalog-clear-done|moveinfo-live-post-catalog
# NPC 可见；可走；可踩门

# 空图：桃花岛进出商城
shop_return_kind2_reenter_arm ... (至多一次)
# 无 rehydrate 循环；可撞怪
```

## 明确不做

- 不按「服务端 already-seeded」跳过出店 27/11
- 不全局对冷 post-catalog 开 2s+kind-2 / 乱调 remaining
- 不改地图石 / 挂机 / 战斗结算

暖目录（priorSeeded）短窗是有证据的收窄，见 `2026-07-28-shop-return-post-catalog-lite.md`。
