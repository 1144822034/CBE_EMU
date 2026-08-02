# 传送后出店：needFb11=0 时注入非空 27/11 + poll 30/2

日期：2026-08-01

Status: implemented

## 现象

非野外图退出商城后不要二次加载卡住。不传送出店正常；地图石传送后再出店曾卡住。

## 根因（运行时取证）

1. 传送后 shop-return **type27 常为 `needFb11=0`**，不请求 `27/11`。
2. 仅注入 empty `27/11` 而 followup 未补非空目录时，DF 打开后无法闭合。
3. 同包 `27/11+30/2` 再只发 `26/0` 仍卡：`26/0` 不能清 DF（见 `2026-07-27-shop-return-loading-stall.md`）。

## 契约

```text
needFb11=1:  type27 empty gate
needFb11=0 && npcnum>0:  type27 注入非空 27/11（无同包 30/2）
followup: 若 warm 已投则 skip；否则非空 27/11
rehydrate: poll 30/2 drip → busy 26/0
npcnum==0: 不注入；shell clear + kind-2
```

## 修改点

- `mock_server_social.c`：shop-return type27 在 `needFb11=0` 且 `npcnum>0` 时注入非空目录。
- `mock_server_interaction_login.c`：followup 暖目录；已注入则 skip。
- `mock_server_equipment_npc.c`：rehydrate 武装 poll `30/2`（非 same-packet-only busy）。

## 验证

```text
shop_return_npc_warm_immediate ... via=type27-inject
# 或 via=followup；随后
shop_return_loading_clear ... remaining↓
shop_return_busy_ack
# 无二次进度条；可走；NPC 可见
```
