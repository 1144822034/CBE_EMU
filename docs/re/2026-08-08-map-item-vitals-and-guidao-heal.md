# 地图用药血条不刷新 + 鬼道清风地图回血

日期：2026-08-08

## 现象

1. 非战斗使用血药后，左上角 WorldHud 血条不涨（背包扣减/提示正常）。
2. 鬼道单体回血「清风拂体」只能在战斗中点，地图上无法使用。

## 证据

- `7/1` 成功只刷新背包（`7/7`/`7/11`），不带 actorinfo；服务端已改 `role->hp`。
- Pixi `session.hp` 仅在选角 / 战斗 vitals 更新；用药路径未写 HUD。
- 场外血条权威同步既有路径：`1/1/14`（`docs/re/2026-07-25-revival-map-vitals-1-1-14.md`）。
- `skill.dsh`：清风拂体 211–215 唯一 `使用限制=1`、`目标指向=1`、生命变化>0；mmGame 有「只能在战斗中使用该技能」字符串，**真服地图施法 WT 仍 unresolved**。

## 根因

1. 用药：服务端改血，客户端 HUD 未同步。
2. 清风：Pixi 硬拦地图施法；无地图回血 handler。

## 修改

1. 用药成功：`note_map_vitals_changed` 武装 `1/1/14`；Pixi 本地按 item.dsh 乐观加血并 `onVitals`；解析 `1/1/14`。
2. 地图回血：provisional `1/12/8 { skillid }` → 扣蓝、按 DSH 治疗、回 `1/1/14+16/2`；仅 `使用限制=1` 且已学技能。Pixi 技能面板对清风显示「回血施放」。

## 验证

1. `make -j2`；残血用地图血药 → 左上角条立即上涨；日志 `map_vitals_note reason=item-use`。
2. 鬼道学清风后，地图技能「回血施放」→ HP 升、MP 降；日志 `mock_map_heal_skill`。
3. 其他技能地图仍提示需战斗中。

## 仍未知

- mmGame 真实地图技能 WT kind/subtype（待 IDA xref `0x5f88`）。
