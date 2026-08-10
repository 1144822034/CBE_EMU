# 怪物 204 后台 HP 不生效（开战仍 1400）

日期：2026-08-08

## 现象

后台将 `#204` HP 改为 `10000`，进战约 `1400` 血即倒。

## 证据

- 内置 `#200` 为灵系 Lv.43：公式 HP `22+43*6=280`，等级倍率 ×5 → **1400**。
- `#203`/`#204` 不在静态 builtin 目录，只在 MySQL `server_monster_catalog_extra`。
- `vm_net_mock_monster_stats_for_enemy_raw` 曾在 `db_load()` **之前**取 catalog index：自定义 ID 得到 `index=-1`，跳过 MySQL 覆盖，退回公式血。

## 根因

自定义怪覆盖未挂上 → 开战用家族公式（与 `#200` 同档时恰为 1400），不是等级倍率把 10000 缩小。

## 修改

先 `vm_net_mock_monster_db_load()`，再 `catalog_index` / 读 override（stats / family / cast_skill / drops）。

## 验证

1. 后台 204 HP=10000 保存；服务日志 `monster_save ... hp=10000`
2. 新开一场打 **enemy=204**（确认不是 wire `#200` / 挑战 `#203`）
3. 开战日志 `enemyhp` / `per_enemy_hp` 应为 `10000 × 等级倍率`（Lv≥40 再 ×5；首领再 ×5）
4. 后台与对战必须是**同一** mock-server 进程/主机

## 续：挑战开战仍读 wire HP（2026-08-08）

`mock_challenge_battle_start` 的 subtype-10 `battleinfo` 曾传入 `enemyWireId`
（常为 `#200`），挂机路径已用 `requestedEnemyId`。结果：服务端槽位可能已是
204 覆盖血，开战包 UI 仍按 200 公式 1400。

已改为与挂机一致：`build_battle_start_info_blob(..., requestedEnemyId)`。

另：PixiJS `FIXED_LIVE_GATEWAY` 可为局域网本机 IP（如 `192.1.1.2`）。

## 续：后台覆盖曾误改为绝对值（已撤销）

曾把 MySQL 覆盖当成「填多少用多少」跳过等级倍率，导致库里按基数保存的怪
开战 HP/攻防整体变矮（体感「属性变少了 / 没读库」）。

**现行契约（与 `2026-07-26-monster-level-combat-attr-multiplier.md` 一致）：**

- 后台 / `server_monsters` 存 **基数**
- 开战：`stats_for_enemy` = 基数 × 等级倍率（首领 HP 再 ×5）
- 管理页列表继续显示基数（`_raw`），避免回写二次放大

验证：后台 204 基数 HP=10000、Lv≥40 → `combat_hp=50000`；不是公式 1400。
