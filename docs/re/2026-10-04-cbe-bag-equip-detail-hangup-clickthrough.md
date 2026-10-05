# CBE 背包装备详细点穿挂机

日期：2026-10-04

## 触发与现象

CBE 模拟器打开背包，点装备「详细」后立刻开始自动挂机。

`bin/client_out.txt`（怀旧服 `106.54.59.92:19097`）：点详细后 `data_request … stage=recv` 重试，`resp=232`/`285` 触发 `cbe_hangup_settle_ui_arm … battle-auto-on-4/11`，随后 `4/7.combatinfo`。

第二轮补丁后仍开战，且**没有** `hangup_rect_touch` / `cbe_hangup_uplink`：详细按钮不在挂机热区内；上行 `Type` 被当成 `enc[0]`（实际是 `00 01 02` 的 tag），识别失败后原样转发。

## 预期 / 实际

- 预期：只打开装备详情；世界 HUD「挂机」只在刚点过挂机热区时发出 `2/10 Type=2`。
- 实际：mmGame 在背包/详细里仍会组挂机开战包；主机未识别或未按「上次点击是否挂机按钮」拦截。

本次只改模拟器，不改服务端。

## 根因

1. 背包/详细常仍是 mmGame（栈深 1，左软「菜单」）。热区吞触摸挡不住热区外的「详细」。
2. CBE 点详细仍可能发出 `1/2/10 Type=2`。
3. 主机用响应字段 `enc[0]` 读 Type，CBE/Pixi 编码是 `00 01 value`，永远认不出 Type=2，所以上行日志也不打。

## 修改

`src/main.c`、JNI `cbeEmu/main.c`、`network-client.c`（桌面 + JNI，`CBE_CLIENT_ONLY`）：

1. 按 mock 的多种 Type 编码识别挂机开战；每次数据上行打 `cbe_uplink`。
2. **只有**上次按下落在野外挂机热区（栈深 1、左软「菜单」、无窗标题）才转发 `2/10 Type=2`。点详细会清 `hangup_arm`，随后开战包被 `swallow_hangup_uplink` 并本地空 WT ack。
3. 每次 `mouse_down` 记录坐标 / `hangup_arm` / title / 左软。

方向键「下」(skey 18) 不拦截。

## 验证

1. 野外点挂机：`mouse_down … hangup_arm=1`，`cbe_hangup_uplink Type=2 forwarded=1`，开战。
2. 背包点装备详细：`mouse_down … hangup_arm=0`；若仍组 `2/10 Type=2` 则 `swallow_hangup_uplink`，出详情不进挂机。
3. 关背包后再点挂机：仍开战。

## 仍未知

若详细路径根本不发 `2/10 Type=2`、只是下行里夹了 `4/11 type=1`，看 `cbe_uplink objects=` 再对症。服务端仍不改。

## 续记：开商城被同一拦截误吞（2026-10-04）

`client_out.txt`：mmShop 上行 `1/2/10,1/14/14,1/14/4,1/14/5,1/14/6` 被 `swallow_hangup_uplink` + 空 WT ack，商城无物品。挂机判定改为仅纯 `2/10 Type=2`（可选 `25/3`/`2/1`）。见 `2026-10-04-cbe-mall-teleport-stall.md`。

## 续记：方向键下误武装挂机（2026-10-05）

`bin/client_out.txt`：背包点装备（`mouse_down x≈127–131 y≈38–58 hangup_rect=0 hangup_arm=0`）先 `swallow_hangup_uplink`，随后 `hangup_softkey_arm evidence=skey18-hangup-intent`，下一发 `2/10 Type=2` 被当成地图挂机并弹出宿主技能菜单。

方向键「下」与挂机键都映射为 skey 18（桌面 `s` / Android D-pad 15→`s`，挂机才是 `c`）。`2026-10-05-cbe-hangup-获取数据-stuck.md` 对任意 skey=18 武装 `hangup_arm`，背包列表用「下」再开「详细」就会开战。

修改：只在 `key=='c'` 时 `hangup_softkey_arm`；方向键下不再武装。点挂机热区仍走 `mouse_down hangup_rect=1`。
