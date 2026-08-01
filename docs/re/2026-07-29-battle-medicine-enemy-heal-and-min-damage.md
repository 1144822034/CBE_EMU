# 2026-07-29 战斗药品加到敌人 / 破防保底伤害 1

## 现象

1. 鬼道等职业用加血技能后，普攻/怪反击常显示伤害 `0`；用户确认**闪避可保持 0**，只需命中后破防仍至少 `1`。
2. 战斗中使用药品时，回血/回蓝飘字与血条落在对面怪物上。

## 根因

### 破防与闪避

`damage_after_defense` / 普攻·技能·怪反击路径在命中后已有 `damage==0 → 1` 保底。
`valueA=0` 仅表示未命中/闪避，按用户要求不改为 1；同时 `child_flag=3` 以触发客户端「闪躲」飘字（见 `2026-07-29-battle-hit-crit-resist.md`）。

### 药品加到敌人

`actioninfo` 正向 `valueA`/`valueB` 打在 **target wire** 上。subtype 5 客户端：
玩家 wire `1`、怪物 wire `0`。

若开战用了 subtype 5，但中途 `use_scene_monster_wire_maps` 因
`SceneMonsterStartActive` 丢失而退回 subtype 10 表（player=`0`），则：

- 药品/治疗 `actor=target=0` → 客户端把回复播到怪物；
- 进攻目标落到错误 wire，飘字也错乱。

`teaminfo` 仍按 `roleId` 写玩家 HP，容易误以为“自己加到了血”，同时看见怪也在加血。

辅助技能还会把 `LastIndex` 记成玩家 wire，自动战斗下一拍可能重放满血治疗
（amount=0），看起来像“普攻伤害 0”。

## 修改

1. 开战冻结 `g_mockBattleStartUsesSceneWireMaps`；武装/结算期间 wire 表只用开战布局。
2. 药品强制自我 wire；若 `playerSlot` 落在存活敌方 wire 上则重定向并打日志。
3. 治疗/buff/封魔不写入 `remember_last_operate`（保留上一进攻技能与敌方目标）。
4. 账户 capture/restore 同步 `mockBattleStartUsesSceneWireMaps`。

## 验证

- `make -j2` / `make server -j2`
- 战斗用药：日志 `actor`/`target` 应为玩家 wire；不应出现怪 +HP/+MP
- 可选：`mock_battle_item_heal_retarget` 仅在 wire 表曾错乱时出现
- 命中破防仍 ≥1；`FORCE_MISS=1` 时伤害仍为 0
