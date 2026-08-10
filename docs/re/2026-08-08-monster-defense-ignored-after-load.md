# 怪物防御已入库但普攻仍按 0 结算

日期：2026-08-08

## 现象

后台 / MySQL `server_monsters.defense_value` 可保存并出现在管理页，开战 HP/攻击已按覆盖值生效，但玩家普攻飘字不随怪防变化（高防怪与低防怪伤害相同）。

## 证据

- `vm_net_mock_monster_db_row` / `monster_admin_save` 写入 `override->stats.defense`。
- `vm_net_mock_monster_stats_for_enemy` 对覆盖行取基数后再乘等级倍率；普攻应使用倍率后的 `stats.defense`。
- `vm_net_mock_battle_player_damage_to_enemy` 却写死  
  `CBE_BATTLE_ENEMY_DEFENSE` 默认 `0`（注释 “was: stats.defense”）。
- 挑战开战日志仍打印 `env_if_set(..., stats.defense)`，与真实结算不一致。
- `docs/re/2026-07-31-monster-defense-zero.md`（2026-08-03）已要求普攻读 `stats.defense`，代码回退未同步。

## 根因

首次偏离在玩家→怪物普攻减伤：配置侧已加载防御，结算侧丢弃为 0。不是加载失败。

## 修改

1. 普攻默认 `defense = stats.defense`（env 仍可覆盖）。
2. 进攻技能（物技+法术）统一走怪物次级抗性百分比（默认 0），与 2026-08-03 契约一致；不再对物技走假的 `defense=0` 护甲路径。

## 验证

1. 后台保存两只怪：同攻击档，防御分别 0 / 较高值。
2. 同角色普攻：高防怪飘字应更低；日志可用 `CBE_BATTLE_ENEMY_DEFENSE` 对照。
3. 物技不吃怪防；`CBE_BATTLE_MONSTER_RESIST=30` 约保留 70% raw。
4. `make -j2`

## 仍未知

- 命中/闪躲/暴击/抗性尚无 `server_monsters` 列，仍为 env/固定次级属性。
- 原始客户端护甲曲线与抗性表仍 unresolved。
