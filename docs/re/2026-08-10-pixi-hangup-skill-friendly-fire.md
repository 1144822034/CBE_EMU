# Pixi 挂机技能误打自己/队友

日期：2026-08-10（续：三职业群攻统一）

## 现象

挂机 + 战斗心得三怪时，群体技能特效/伤害落到本方。天机（雷震八方）修好后，幻剑（荒魂劫火）/鬼道（天火熔身）仍可能复现。

## 根因

与职业无关：挂机 subtype-10（玩家 wire=0）若仍开场景线槽，群攻 child `[2,3,0]` 的 wire `0` 会画到玩家。三职业敌群技均为 `目标指向=4`，同一路径。

另：挂机环再开场会把 `LastIndex` 重置为 `0`；在 subtype-10 上 `0` 是玩家，需按本场线槽重绑。

## 修改

| 层 | 内容 |
|----|------|
| 服务端 `apply_player_attack_targets` | subtype-10 时清掉残留 `StartUsesSceneWireMaps` |
| 服务端 hangup start | 无新 Operate 时按本场 `defaultEnemySlot` 重绑 `LastIndex` |
| Pixi 播报 | 多段伤害 child 若落到己方 → 改锚存活敌人；群攻 FX 禁止回退施法者 |
| 挂机技能菜单 | 只列进攻技（目标 3/4 且非加血），避免误选清风拂体等 |
| `buildHangupStartRequest` | 有坐标即带 live-node，不依赖 id≠0 |

## 验证

1. 天机/幻剑/鬼道：挂机选各自敌群技 + 心得三怪 → 三只均有特效与伤害，不落玩家。
2. 挂机菜单无治疗/自 buff。
3. `make -j2`；Pixi `npx tsc --noEmit`。
4. 重启 mock 服务端后再测。
