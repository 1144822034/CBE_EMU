# 强化加成显示与 common-extra（2026-07-29）

## 现象

装备强化后，原生背包/装备详情主属性行（`c0[物攻]:c4+%d` 等）仍是
`equip.dsh` 裸基值；客户端强化表（`global+0x580+4`）为 null，主行不加算。

## 强化附加

`vm_net_mock_seq_put_item_common_extra`：

1. **`L>=4`**：部位里程碑 4 槽，unlock `4/8/12/16`（灰显规则不变）。
2. **`L>=1`**：其后追加 `M(L)-base` 扁平加成，仅当 `equip.dsh` 对应列基值 > 0：
   顺序 **护甲 → 物攻 → 气血 → 法力**；`unlock=1`（有强化即亮色）。
3. 客户端词条容量 **6**（`ParseEquipAttributes` `cmp #6`）：里程碑优先，
   追加槽占剩余（`L>=4` 时最多再加 2 条；`1<=L<4` 时可下满 4 列）。

```text
i16 enhance | i16 maxlevel|(attr_count<<8) | u8 attr_count
槽：u8 unlock, u8 type, u8 flag, i16 value
```

穿戴汇总与商店/出售「查看属性」仍用 `M(L)`（`mock_server_catalog.c`）。

## 人物装备界面（菜单 → 玩家信息 → 装备）

子菜单字面量在 `江湖OL.CBE:0x010221C6`：`称号 / 背包 / 任务 / 法术 / 装备 / 属性`；
构建入口 `0x01021FD8`。点「装备」打开**本机穿戴栏**（槽名
`帽子…武器` @ `0x0103231E`，空槽文案 `未装备`），**不**发 `29/4` /
`10/2` 等查看包——穿戴件已在登录/换装的 `7/7 type=2` 里进 item-manager
（category 15）。

| 界面 | 服务端能否改 | 路径 |
| --- | --- | --- |
| 身上有哪些装 / `(+N)` | 能（已下发） | `7/7 type=2` + common-extra |
| `强化附加` 词条 | 能（L≥4 已下发） | 同上 common-extra |
| 点开单件主行 `[物攻]:%d` | **不能** | 本地 `equip.dsh`；强化表 null |
| 同级菜单「属性」总攻防 | 能（已计入） | `actorinfo` + `M(L)` |
| 看他人装备 | 能下词条，主行同左 | `29/4` `equipinfo` |
