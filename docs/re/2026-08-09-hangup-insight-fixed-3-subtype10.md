# 挂机 + 战斗心得：subtype-10 未固定 3 怪（2026-08-09）

## 现象

玩家使用战斗心得（828）后挂机，遇怪数量仍为 1（或未固定为 3）。

## 根因

1. Pixi/APK 挂机多数无 emulator live-node → `mock_hangup_battle_start` 走
   `target_source=non-scene-subtype10`（`useSceneMonsterStart=false`）。
2. `vm_net_mock_battle_roll_enemy_count` 原先在 insight 判定**之前**对
   `!useSceneMonsterStart` 直接 `return 1`，828 固定 3 永不生效。
3. 开战态又写 `g_mockBattleEnemyCountCurrent = useScene ? rolled : 1`，
   subtype-10 `battleinfo` 也硬编码 `left_count=1`。
4. 非场景 wire 表只认单个 `fallbackEnemySlot`，即使 count=3 也无法打多只。

## 修改

| 点 | 文件 |
|----|------|
| 掷点：env → insight=3 → 非场景=1 → 随机 | `mock_server_equipment_npc.c` |
| subtype-10 `battleinfo` 支持 `monsterCount` | `mock_server_social.c` |
| hangup/challenge 写入真实 `enemies`；场景 wire 仅 subtype-5 | `mock_server_battle.c` |
| subtype-10 多怪 wire：player=0，怪=1..N | `mock_server_core.c` |

## 验证

1. `make -j2 server`，重启。
2. 使用 828 后挂机：日志
   `mock_battle_enemy_count_fixed ... reason=battle-insight+subtype10`（或无
   `+subtype10` 的 scene 路径），且 `mock_hangup_battle_start ... enemies=3`。
3. 战斗画面 3 只怪；需打完三只才结算。
4. 未开 828：挂机 subtype-10 仍 `enemies=1`；场景遇怪仍 1..3 随机。
