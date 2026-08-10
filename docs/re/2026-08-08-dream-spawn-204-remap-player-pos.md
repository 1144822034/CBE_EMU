# 梦境刷怪点 204：踩怪仍见 #200 公式血 1400

日期：2026-08-08

## 现象

后台 `29梦境空间_01` 刷怪点目录 ID=204，踩怪 UI/倒地仍约 1400。

## 权威数据

| 表 | 内容 |
|----|------|
| `server_scene_combat_spawns` `_01` | slot0 `(80,154)` / slot1 `(137,253)`：`wire=200` → `real=204` |
| `server_scene_monsters` `_01` | 挂机槽 `204,204,0` |
| `server_monsters` 204 | 当前 hp=1000（基数；Lv≥40 开战 ×5 → 5000） |
| SCE field `0x0E` | 仍为线号 `#200`（ParseMinfo 安全键） |

1400 = 内置 `#200` 灵系 Lv43 公式，不是客户端本地编造。

## 根因

1. 地图线号与后台真实 ID 分离；开战必须 `wire→real` remap。
2. Pixi `startBattleNearPlayer` 曾把**玩家坐标**放进 `posx/posy`，对不上绑定表精确坐标。
3. 同图两个刷怪点都是 `wire=200→real=204` 时，旧逻辑要求 `wireOnlyCount==1` 才回退，多点一致时仍退回 `#200`。

## 修改

- `mock_server_scene_task.c`：同 wire 且 real 一致（unanimous）时 remap 到该 real。
- `GameSession.startBattleNearPlayer`：请求携带最近刷怪点坐标。
- **取消静默退回线号**：场景存在 `wire≠real` 绑定时，remap 失败返回 `0` 并打
  `remap_miss` / `remap_ambiguous`；开战 `reject-remap`（提示「刷怪绑定失败…」），
  不再用 `#200` 公式血开打。商城回梦境 followup 同样禁止 `enemyId=wireId`。

## 验证

1. 重启 mock；梦境 `_01` 踩怪。
2. 成功：`mock_scene_combat_spawn_remap … wire=200 real=204`；
   `mock_challenge_battle_start requested=204 … combat_hp=` = 基数×等级倍率。
3. 失败应可见：`remap_miss` / `reject-remap`，客户端提示绑定失败，**不会**再出现 1400。
4. 挑战 NPC `32901` 仍绑 `challenge_enemy_id=203`，与刷怪点 204 无关。
