# 商城购买逍遥壶/神仙壶后卡 loading

## 症状

背包曾满，整理出空位后再买 `803 逍遥壶`（或 `802 神仙壶`）：服务端
`mock_shop_buy14 ... result=1` 已扣酷宝并入库，客户端商城一直转圈卡住。

## 触发与首次偏离

1. 包满时购买失败（应为 `result=3` 提示整理背包）。
2. 清理背包后再次购买秘宝壶。
3. 服务端成功：`seq>0 result=1`，`wcoin` 减少。
4. 客户端停在商城 loading；若此时误进充值，可见 Wpay `DoLoading` /
   `transfer_sub_bcc kind=0`（游戏 WT 被支付屏误收），那是后续症状。

首次偏离在 **`14/3` 成功解析之后**：壶类成功分支未清 shop loading 标志。

## 根因（mmShop 契约）

`mmShopMstarWqvga.cbm:sub_9DE` 对 `14/3` `result=1` 且商品为 802/803：

1. 走本地插入后，在 `0xD90` 置 flask 标志（`sp+0x44=1`），UI 状态清 0。
2. 响应收尾 `0x104C..0x1052`：**仅当该标志为 0 时**才
   `strb #0 → [shop+0x34]+0x10` 清除 loading。
3. 壶类标志为 1 → **跳过清 loading**。
4. 同函数对 `7/11`（`0xEC6..0xF6C`）会清 flask 标志，并在 UI 状态为 0 时
   恢复为 4，随后正常收尾才能清 loading。

旧响应只有单对象 `14/3`，壶类买成功后 loading 永不落下。

普通道具不置该标志，收尾会清 loading，故「清包后再买普通货」不一定复现。

## 修复（已修订）

**错误尝试 1：** 末尾追加 `1/1/14 actorinfo` —— 能清 loading，但会退出商城。  
**错误尝试 2：** 同包追加 `7/11` —— 能走 flask 收尾，但**新壶进不了背包**。

**正确契约：** 即时只回 `14/3`；≥2 tick 后由场景 poll 单独投递 `7/11`
（`shopFlaskLoadingClearPending`）。详见
`2026-07-27-shop-buy-flask-exit-mall.md`（修订 5：DSH `consumeMode=0`
曾导致第二把壶叠进旧行，需先保证 `add_backpack` 新开 `seq`）。

## 验证

1. `make -j2` 通过。
2. 背包有空位时买逍遥壶：即时 `resp=14/3+pending-7/11`，随后
   `shop_flask_loading_clear_deliver`；壶在背包；loading 落下；留在商城。
3. 包满再买：仍为 `result=3`，不扣费，提示整理背包（非酷宝不足）。
4. 买 801/800 等非壶秘宝：仍为单对象 `14/3`（除非原有 map-revive/808 特例）。
