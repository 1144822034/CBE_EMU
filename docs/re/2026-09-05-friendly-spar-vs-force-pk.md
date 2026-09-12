# 2026-09-05：友好切磋 vs 强制PK

phase: friendly-spar-vs-force-pk
status: implemented

## 契约

| 菜单 | 请求 | 行为 |
|------|------|------|
| 切磋 | `1/4/14 { id }`（无 force） | 旧邀请链：enqueue `SPAR_INVITE` → 对端 `4/15` → `4/16`(+可选 `4/9`) 同意后开战 |
| 强制PK | `1/4/14 { id, force:1 }` | 直开战（原 2026-08-07 附近切磋）；发起方扣 **强制PK证明×1** |

### 切磋（友好）

- 不走人偶、不扣强制PK证明
- 不记发起 PK、不红名、无 5 分钟冷却
- 失败双方无经验/金钱/传送惩罚；不记恩怨
- 友好切磋胜利计入跑环「击杀玩家」进度（职业+等级段匹配；按胜者账号落库）
- `duel->friendlyMode = true`（同意开战 / ready 开战时置位）

### 强制PK

- 保留替身人偶（result=8）、发起冷却（result=9）、PK 满（result=10）
- 缺证明 → `result=11`「缺少强制PK证明」
- 开战成功：扣证明、+20 PK、武装 5 分钟冷却；负方仍走死亡惩罚与恩怨
- **不计入**跑环击杀玩家进度（仅友好切磋计）

### 共享门闩

- 城镇 / 比武擂台图 → result=7
- 任一方 `<25` → result=6
- 忙碌 → result=3

## 道具

- `itemId=848`「强制PK证明」：耐久 1，商城类别 14，默认酷宝 30
- 服务端 `ensure_force_pk_proof_catalog_item`；Pixi `itemCatalog` 注入同名

## 修改点

| 层 | 文件 |
|----|------|
| 服务端 | `mock_server_guild.c` 双路径 `4/14`；invite reply / ready 置 `friendlyMode` |
| 服务端 | `mock_server_equipment_npc.c` 证明消耗、友好离场无惩罚 |
| 服务端 | `mock_server_battle.c` / `enmity.c` 跳过友好环任务与恩怨 |
| 服务端 | `mock_server_catalog.c` / `role.c` / `core.c` 848 |
| Pixi | `SocialPanel` 切磋+强制PK；`protocolSocial` force 字段与 4/15/16；dialog 262/263 |

## 验证

1. A 切磋 B → B 弹「请求和你切磋」→ 同意双方进战；拒绝 A 见不同意 tip；无人偶/证明消耗、无 PK 变化
2. 友好切磋打死一方：败方无经验/金钱扣减、不传送石、不进恩怨；若胜者有匹配职业/等级段的跑环击杀玩家任务则进度 +1
3. A 强制PK B 且持有 848：扣 1、双方进战、A +20 PK；再发起 5 分钟内 result=9；**环任务进度不加**
4. A 无 848 强制PK → tip「缺少强制PK证明」
5. B 持人偶，A 强制PK → result=8，不扣 A 的证明
6. 友好切磋不触发人偶

## Negative

- 勿把友好切磋再改回直开战
- 勿对友好路径加 PK / 冷却 / 人偶
- dialog action 勿占用挂机技能用的 260
- 勿再把门闩改回「仅强制PK计环」（2026-09-06 起改为仅友好切磋计环）
