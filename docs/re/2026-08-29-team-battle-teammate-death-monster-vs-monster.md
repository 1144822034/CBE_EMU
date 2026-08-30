# 组队战队友死后怪互打（2 人 + 3 怪）

日期：2026-08-29

## 症状

双人/三人组队打多怪（典型 3 只）时，一名队友阵亡后战斗画面错乱，怪物像在互相冲刺攻击。

## 契约

组队 subtype-5 通用置换（不限 2/3 人）：

```text
display_to_wire(d) = d >= M ? d - M : P + d
wire_to_display(w) = w < P ? M + w : w - P
```

**M=3, P=2**

```text
队长 → wire 2
队员 → wire 3
怪0  → wire 4
怪1  → wire 0
怪2  → wire 1
```

**M=3, P=3**

```text
席0/1/2 → wire 3/4/5
怪0/1/2 → wire 0/1/2
```

正常反击：actor 为怪 wire，target 为存活队员 wire。

单人三怪表：玩家=`1`，怪=`[2,3,0]`。该表落到仍按组队表解包的 Pixi 时（2p 或 3p 皆然）：

| 包 wire | P=2 组队解成 | P=3 组队解成 |
|---------|--------------|--------------|
| `0 → 1` | 怪1 → 怪2 | 怪0 → 怪1 |
| `2 → 1` | 队长 → 怪2 | 怪2 → 怪1 |
| `3 → 1` | 队员 → 怪2 | 席0 → 怪1 |

P=3 时前两条直接是怪打怪；本次拦截不依赖人数。

## 根因

第一次偏离在**服务端出包路径**，不是客户端 AI：

1. 组队 `prepare` 写入 `party_count` / `member_count`（= 冻结 `battleMemberCount`，2 或 3）。
2. `operate` builder 失败（`return 0`：self-target abort、actioninfo 编码失败等）后，`clear_operation_context` 清掉 party scratch，但 `StartUsesSceneWireMaps` 仍为 1。
3. `synchronized_team_battle` 把 0 交给 dispatch；末尾 **solo lastchance** / `raw82` 再跑单人 operate。
4. `member_count==0` 时 `enemy_wire_for_index` 走单人多怪表 `[2,3,0]`、`playerSlot=1`。
5. Pixi 开战花名册仍冻结 `P>=2,M`，冲刺解成怪→怪。

队友死亡回合更容易踩中：死亡 type-3、多怪反击、alive_mask 收缩、合并/orphan，任一编码失败都会打开上述漏洞。另：`prepare` 因 `battleMemberClientIds` 对不上而 inactive 时，原先也会直接落到单人 operate。

次要：Pixi `playActionScript` 文档要求的 `isUnitDefeated` 门闩缺失，后手会对已败目标继续冲刺，加重「场面乱」（见 `2026-08-17-team-battle-attack-anim-on-dead-monster.md`）。

## 修改

1. `mock_server_battle.c` `synchronized_team_battle`：
   - 队伍 `battleActive` 但本席不在 battle roster → 空 WT ACK，禁止 solo（**不限 2/3 人**）。
   - prepare 后 builder 返回 0 → `team_battle_round_wait`（`operate-builder-failed`），禁止清 scratch 后交给 lastchance。
2. `mock_server_dispatch.c`：组队战进行中拦截 `lastchance-fallback` / `raw82`（判 `battleActive`，非 `party==2`）。
3. 反击编码：若 `party>=2` 且 counter target 仍是活敌 wire → `mock_battle_counter_monster_vs_monster` 错误日志。
4. `BattleScreen.playActionScript`：恢复 `isUnitDefeated`；进攻目标全灭则跳过冲刺/特效/飘字；怪→怪冲刺打 `console.error`。

## 验证

1. `make -j2`
2. 两人/三人组队遇 3 怪；一人阵亡后继续打：怪应冲向存活玩家，不应互啄。
3. 日志不应再出现 `builtin-battle-operate-lastchance-fallback` 紧跟组队 `team_battle_round_prepare`；若 builder 失败应见 `team_battle_operate_builder_failed` / `lastchance_blocked`。
4. 正常单人三怪、未死人的组队回合动画不变。

## unresolved

- 偶发 builder 失败的具体 abort 原因仍需下次日志里的 `mock_battle_operate_abort` 对齐。
- 开战冻结 `partyCount`/`monsterCount` 进 `BattleState`（不靠数组长度）属加固另案。
