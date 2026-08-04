# 2026-08-04 上线已穿装卸下不减 / 再穿双加 / 商城退出闪退

## 现象

- 角色上线时已穿装备：卸下后面板三围等不减；再穿会在旧值上再加一份。
- 换装后进商城再退出闪退。

## 运行时证据（session a1d304）

| 假设 | 结果 | 证据 |
| --- | --- | --- |
| 卸光服务端已裸 | **确认** | `mock_item_equip type=4` `agi=146` `eq=[0…]` |
| 面板仍粘登录满值 | **确认** | 出店后 `w1_agi=164` + `wearBare=1`，卸装仍 `r11=164` |
| 再穿双加 | **确认** | 随后装备 `H3_skip agi=182`（164+18） |
| 进店前穿脱正常 | **确认** | 146→164→146→164；`equip_vitals_delivered=0` |
| shop-return 未完成 | **确认** | 有 `shop_open`，无 `shop_return_rehydrate`；约 21s 后 `live=0` 重登 |
| emu_crash | **排除** | 无 `S4_crash`；客户端 `程序已正常退出` |
| 商城期间 host realign | **排除（该次）** | `shop_open` 与重登之间无 `wear_epilogue` |

## 根因

1. **登录 actorinfo 三围种满值**：5CA4 基线已含装备；卸装只改 5CAC → 粘滞；再穿再叠 jump → 双计。护甲词本来就是裸种+F8，故护甲卸装正常。
2. **商城退出软复位**：客户端在发出带 `mmShop=1` 的退出 `5/10` 之前就回到登录态；服务端看不到 `shop_return_rehydrate`。

## 修复（本轮，待复测）

1. **恢复** `InitSceneViewport`（`0x01022426`）面板字符串 realign：wear 尾声常 `vb=0`，去掉后属性页力量显示 0（本轮日志无 `P4_fix`）。
2. **撤回** 登录裸三围种子（equipment_login 后仍 `wearBare=1`，裸种+无 wear 更差）；登录仍发满值三围。
3. 仍不写 actor `+122/+124/+126`；`wearBare` 仅在有效 bare 快照时覆盖。
4. shop-return / `S7`/`S8`/`S9_viewport` 取证日志保留。

## 验证

1. 打开属性页：力量/敏捷/智慧为正常非 0；日志有 `S9_viewport` + `P4_fix` 且 `r10` 非 `"0"`。
2. 穿脱裤子：三围跟随变化（卸装回落；再穿不双加为下一目标）。
3. 进商城再退出：观察是否仍闪退；期望有 `shop_return_rehydrate`。
4. `make -j2`
