# 查看附近玩家只显示一件装备

phase: nearby-player-equip-list
status: fixed

## 触发

周围玩家 / 好友菜单点「玩家信息」。Pixi 把 `10/2` 姓名等级帮派和 `29/4` 装备拼在同一屏，结果装备区往往只有第一件。

## 根因

`29/4` 的 `equipinfo` 每行是 `u32 itemId + i16 seq + ParseEquipAttributes`。装备行在词条后还有 Pixi 宝石孔尾（tagged `u8 0x47` + 三个 tagged `u8`）。`parseOtherEquip` 的 `skipItemCommonExtra` 停在 `attr_count`，下一行的 tagged `u32` 读到 `00 01` 直接抛错，循环中断，界面只留下第一件。

证据：`src/server/mock_server_catalog.c` 的 `vm_net_mock_seq_put_item_common_extra` 注释写明「Pixi must consume these or the next row desyncs」；背包/穿戴解析 `readItemCommonExtra` 已经吃掉这段尾，附近装备解析没有。

## 修改

- `PixiJS_web/src/net/protocolSocial.ts`：`skipItemCommonExtra` 遇到 `0x47` 尾则跳过，后续装备行继续解析。
- `PixiJS_web/src/main.ts`：「玩家信息」不再请求、不再拼接 `10/2`。只拉 `29/4`，按帽衣披束裤鞋饰品武器列出名称和强化。
- `PixiJS_web/src/ui/SocialPanel.ts`：「查看装备」同样只列装备，不再加等级职业性别行。

## 未改

CBE `HandleEquipInfoResponse` 仍按 `attr_count` 停止，不读宝石孔尾。多件装备的 `29/4` 对 CBE 纸娃娃仍可能从第二件起错位。这次只修 Pixi 列表。
