# CBE 点挂机卡「获取数据」

日期：2026-10-05

## 触发与现象

野外点挂机（软键 `c`/skey18 或热区边缘）后界面一直停在「获取数据」。

## 根因

1. `HandleBattleEnterReq` 发 `2/10 Type=2` 前已把进入态置为 `3`（「获取数据」）。
2. 主机 `should_block_hangup_uplink` 在 `hangup_arm=0` 时吞掉上行，原先只本地空 WT ack。
3. 空 WT **不能**清 enter-state=3，进度条永久卡住（同 `2026-07-28-hangup-button-delay-stop-after.md`）。
4. **点不到挂机（`bin/client_out.txt`）**：UI7 坐标点中（x≈44–65 y≈340–354）但
   `stack=3`、`left=""`、`world_hud=0` → `swallow=1`，触摸根本不进 CBE。
   mmGame 自绘软键栏，Ctrl `g_softkeyLeftCache` 常为空；登录后屏栈常 ≥3。
   用 `stack==1` / 左软「菜单」当门闩是假阴性。

## 修改

`src/main.c`、JNI `cbeEmu/main.c`、`network-client.c`（桌面 + JNI，`CBE_CLIENT_ONLY`）：

1. 去掉 hangup 触摸/上行对 `stack>=2` 与 `world_hud`（左软菜单）的依赖；只认
   `bag_ui`（窗标题 / 使用 / 详细）+ `hangup_arm`。
2. 热区收回 UI7：x 36–96 / y 338–366 @240×400（避开聊天区）。
3. 仅挂机键 `'c'`（非方向键下，虽同为 skey 18）在非 bag_ui 时武装 `hangup_arm`。
   见 `2026-10-04-cbe-bag-equip-detail-hangup-clickthrough.md` 续记。
4. 误吞时本地投递 `2/10(empty)+25/12(result=4)`，不再用空 WT。

合法挂机仍转发，并在上行时 `hangup_settle_ui_arm`（战斗心得面板，见 `2026-10-05-cbe-hangup-settle-combatinfo-panel.md`）。

## 验证

1. 野外点挂机：`hangup_rect=1 hangup_arm=1 swallow=0`（允许 `stack=3 left=""`），
   `cbe_hangup_uplink … forwarded=1`，进战斗。
2. 背包点装备详细仍组 `2/10 Type=2`：`swallow_hangup_uplink … ack=2/10+25/12`，
   不进挂机、不卡进度条。
3. 开江湖商城：仍是 `14/*` 批量上行，不被挂机吞。

## 相关

- `2026-10-04-cbe-bag-equip-detail-hangup-clickthrough.md`
- `2026-10-04-cbe-mall-teleport-stall.md`
- `2026-07-28-hangup-button-delay-stop-after.md`
- `2026-10-05-cbe-hangup-settle-combatinfo-panel.md`
