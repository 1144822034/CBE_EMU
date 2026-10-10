# 出商城二次加载卡住 + 切图 poll 微调

Date: 2026-10-10

## 日志（`bin/client_out.txt`）

### 切图

- `scene_poll_urgent_arm` 已生效；剩余耗时主要是 `network_ms≈150–500`（RTT/短连重试），不是 2s poll 下限。
- 误触发：门户后 `16/3+27/11+27/4`（len≈59）也被当成确认 → 已收紧为仅短包 `16/3`（`<40`）或 `16/2`/`16/4`。
- urgent ready 150→100ms（对齐服务端 same-tick≈100ms）。

### 出商城

1. `shop_return_skip_bar_arm` → DF `DoLoading` → `ScreenInit Ok`（第一次，正确）。
2. `27/4` `resp=110` 带 **WT30/1** `allow_map_stone_reenter=1` → 第二次 EnterScene。
3. `same-reenter-allowed-once`（27/12）→ 第三次；金条/深度易卡住。

契约：有 NPC 的户外图出店 **禁止** kind-2 `30/1`（见 `2026-10-05-shop-return-same-scene-30-2-hold.md`）。

## 修改

1. **服务端** `arm_shop_return_kind2_reenter`：`npcnum>0` 直接 skip（勿仅依赖 deliver 时 cancel）。
2. **客户端** mmShop pop 后 ~15 tick：`shop-return-suppress-kind2` 挡户外 `01018150` EnterScene；不武装 map-stone reenter。
3. urgent poll 启发式收紧（上）。

## 验证

- 重启 `jh-online-server`（连远程需部署服务端；客户端单独也能挡二次 EnterScene）。
- 出江湖商城：`shop_return_skip_bar` → 一次 DF `ScreenInit`；**无** `shop-return-suppress-kind2` 失败后的二次 `ScreenInit`，或见 suppress 日志后地图可走。
- 空图出店仍允许延迟 kind-2（窗口外）。
- 地图石：`scene_poll_urgent_arm` 后约 100–300ms 内 poll，无 post-enter 长包误 arm。
