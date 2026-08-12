# 野外 PK 值 / 红名（2026-08-12）

Date: 2026-08-12

Status: implemented (build ok)

## 1. 需求

1. **发起**普通切磋成功时：发起方 `pk_points += 20`（不论胜负；擂台 / 赌战不计）。
2. `pk_points` 上限 **100**；等于 100 时 **不允许再发起**切磋（4/14 `result=10`）。
3. `pk_points >= 60` → 红名。
4. 有 PK 值时：每打赢 3 次野外怪战（PvE settle victory）`pk_points -= 1`。
5. 红名死亡：经验与金钱掉落惩罚加倍（相对普通死亡 10% 级内经验 / 5% 金钱 → 20% / 10%）。

## 2. 数据

- MySQL `account_roles.pk_points`（已有）；新增 `pk_pve_streak`（0..2）记消红进度。
- 内存 `vm_net_mock_role_state.pkPoints` / `pkPveDecayStreak`；会话 `onlinePkPoints` 供附近列表。

## 3. 协议（Pixi-only）

| 位置 | 字段 | 说明 |
| --- | --- | --- |
| 2/10、2/7 | `pkinfo` | `{u32 actorId, u32 pkPoints}×N`，CBE 忽略未知名 |
| 1/1/21 | `pkpoints` | 自身 PK 值，刷新本地名牌颜色 |
| 4/14 | `result=10` | 发起方 PK 已满，拒绝切磋 |

红名判定：客户端 `pkPoints >= 60` → 名牌红色。

## 4. 修改点

- `mock_spar_request`：`pk>=100` 拒发起；`duel_begin` 成功后发起方 +20（cap 100）并落库。
- ~~击杀加分~~ 已取消；恩怨仍按击杀记账。
- `mock_battle_settle` 首次胜利结算：`note_pve_victory_pk_decay`。
- `vm_net_mock_role_apply_death_penalty_to`：红名加倍 percent。
- Pixi：`PeerActor.pkPoints`、ActorLayer / SceneView 名牌色；发起满值本地拦截。

## 6. 2026-08-12 follow-up：切磋卡在「请求攻击」

现象：开战成功后出手，战斗条停在「请求攻击…」。

切磋契约：先出手方 `4/2` → `duel_round_defer`（空 WT），等对方也提交后才发 `4/6`；客户端 `awaitingRound` 锁菜单。提示「请求攻击」易被当成请求失败。

回归嫌疑：发起成功时在组包路径同步 `role_db_save`（整表背包/装备落库）。同类问题见 `2026-07-27-item-discard-continuous-freeze-mysql.md`：公网 MySQL 会拖死热路径，对方 `4/10` 进战变晚，发起方已 defer 空等。

修复：PK 只 `mark_inventory_dirty`，断线/CBMR 后再 flush；切磋等待文案改为「等待对方出招…」。
