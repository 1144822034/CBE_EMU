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

## 未改（已于 2026-10-09 补）

CBE `HandleEquipInfoResponse` 仍按 `attr_count` 停止，不读宝石孔尾。连升级服时多件 `29/4` 从第二件起错位，界面常只剩武器。

### 2026-10-09 主机侧修复

`src/network-client.c`（及 JNI）：对下行 `1/29/4` 重写 `equipinfo`，用与 `30/21`/`7/7` 相同的 `copy_common_extra_cbe` 去掉 `0x47` 宝石尾，并按实际解析行数回写 `num`。

验证：

1. 日志：`cbe_backpack_clamp object=29/4` + `cbe_29_4_row`（多行）
2. CBE「查看装备」纸娃娃/列表显示对方全部穿戴槽，不只武器

### 2026-10-09 姓名后多出 `work`

`work` 是 `29/4` 下一字段的 **字段名**（职业索引 u8），不是要显示的文案。  
live 的 `name` 常为裸 GBK；CBE 按 mock 契约读 `BE16 长度 + 正文`，把姓名前两字节当长度，读穿到下一字段键 `work`。

主机重写时把 `name` 规范成 `BE16+GBK(+NUL)`（`name_form=raw|blob-be16|…`）。

2026-09-21：装备行解析已能读完多件后，Pixi「玩家信息」重新拼接 `10/2` 资料与 `29/4` 装备。见 `docs/re/2026-09-21-nearby-player-info-profile.md`。
