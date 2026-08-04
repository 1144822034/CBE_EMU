# 2026-08-04 穿上装备后多件无法卸下（7/8 seq）

## 现象

登录已穿戴时可卸下；从背包穿上武器/铠胫等多件后，卸载失败（客户端无反应或
失败），服务端 `mock_item_equip type=4 result=0 item=0 slot=255`。

## 根因

`BuildGameEventPacket` subtype 8 请求体只有 `{type, seq}`，**无 itemId**。
卸装完全靠 `seq` 定位穿戴槽。

- 登录 `7/7 type=2` 穿戴行：`seq = slot + 1`
- `7/9` 换装 `body`：同样要求 `slot + 1`
- `7/8 type=3` 成功响应：曾错误地把**原背包 seq** 写入响应；客户端写入
  `item+276`，之后卸装带着背包 seq 来。多槽已穿时 `seq∉[1,8]` 且无法消歧 →
  `ambiguous-seq` / `equipped-item-not-found`。

单件穿戴时「仅一件可消歧」偶然还能卸；多件必失败。对照日志：登录态卸武器
成功，再穿 40065+40071 后 type=4 连失败两次。

## 修复

`vm_net_mock_role_equip_backpack_item`：`equippedSeqOut = slot + 1`。

## 验证

1. 空槽穿武器，再穿铠胫；分别卸下均成功。
2. 日志：`type=3 ... resp_seq=1`（武器）/`resp_seq=6`（腿）；`type=4 result=1`。
3. `make -j2`
