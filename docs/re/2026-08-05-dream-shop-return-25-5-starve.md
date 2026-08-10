# 梦境出商城卡住：dream-25/5 饿死 post-catalog 30/2

日期：2026-08-05

## 现象

在 `29梦境空间_*` 内打开商城再退出：进度条不消，可走性丢失。

## 日志证据（`lxh001` / `845f28ce`）

```text
shop_return_rehydrate … scene=29梦境空间_01 …
shop_return_loading_clear_settle … post_catalog=0 via=2s-no-moveinfo
mock_shop_return_npc_catalog_deliver … rearm_clear=1 post_catalog=1 prior_seeded=0
shop_return_loading_clear_arm … remaining=8 post_catalog=1
mock_shop_return_loading_clear … remaining=7   ← poll 偶发
mock_teleport_stone_direct_enter_ack … action=dream-25/5-no-second-enter
mock_shop_return_loading_clear … remaining=6
mock_teleport_stone_direct_enter_ack … dream-25/5-no-second-enter
… 交替至 remaining=1，中间几乎全是空 result=4 ACK
# 无 shop_return_loading_clear_via path=25/5
# 无 shop_return_kind2_skip via=post-catalog-clear-done / busy_ack
```

## 业务链路

户外 post-catalog 契约（`2026-08-01-shop-return-post-catalog-25-5-drip.md`）：

1. shell `30/2` → deferred `27/11`（第二次 DoLoading）→ post-catalog `30/2` drip → `26/0`
2. 客户端在二次 loading 期间常以 `25/5` keep-alive 为主；`scene_default_event` 须优先投 `30/2`

梦境 dispatch 顺序：

```text
teleport_stone_direct_enter_ack   ← 先命中（29* 恒空 ACK result=4）
scene_default_event               ← 户外 25/5→30/2 修复所在；梦境永远走不到
```

## 根因

- **触发**：梦境内退商城，cold/deferred `27/11` 打开第二次 DoLoading。
- **违反契约**：`dream-25/5-no-second-enter` 吞掉全部短/lone `25/5`，不投递
  pending shop-return `30/2`；户外靠 `25/5` 的 drip 路径失效。
- **首个错误状态**：post-catalog `remaining>0` 时客户端 keep-alive 只收到
  `25/5 result=4`，ResetDownloadState 节奏被饿死 / 半开 DoLoading 无法收尾。
- **证据**：同上日志；dispatch 中 `direct-enter-ack` 在 `scene-default-event` 之前。

## 修改

`vm_net_mock_build_teleport_stone_direct_enter_default_ack_response` 梦境分支：

| 条件 | 行为 |
|---|---|
| `shopReturnLoadingClearPending` 且 clear 到期 | 投递 `30/2`（`path=dream-25/5`） |
| clear pending 但未到期 | **空 WT** keep-alive（禁止 `result=4`） |
| `mapStoneLoadingClearPending` | 同理优先 `30/2` |
| 否则 | 原 `dream-25/5-no-second-enter` result=4 |

## 续：二次加载仍卡（2026-08-05）

`25/5` 饿死修复后，日志仍见：

```text
shell remaining=0 → catalog prior_seeded=0 post_catalog=1 → drip → busy_ack → idle-timeout
```

根因升级为：shop-open 清全局 `npc_seeded` 后 deferred catalog **误判 cold**，
单独 `27/11` 开二次 DoLoading。见
`2026-08-05-dream-shop-return-false-cold-second-load.md`（`resources_warm` →
同包 `27/11+30/2`）。
