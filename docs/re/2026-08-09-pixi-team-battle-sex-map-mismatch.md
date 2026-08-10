# Pixi 组队开战：战斗队友性别与地图不一致

## 症状

组队进战后，右侧队友（或底栏头像）性别/体型与进战前地图上该玩家不一致。

## 根因

战斗 `4/5` battleinfo 右侧席只有 `id + HP/MP`，**没有** job/sex。造型依赖客户端侧：

1. `5/10`/`5/3` 队伍 roster 的 `sexGroup(1..2)` / `jobIndex`
2. 缺失时回落到本地观察者造型

偏离点：

1. **`ensureSelfTeamRow` 硬编码 `sexGroup: 1`（男）**  
   接受邀请（`5/3` delta 不含自己）时本地补的自己行性别错，开战把自己画成男体，地图仍用 `RoleRow.sex`。
2. **底栏 `BattlePartyHud` 把 `right[0]` 当成自己**  
   组队 `right[0]` 是队长。非队长观察者会把本地名字盖到队长槽，再叠加 `sex ?? identity`（默认曾为女）时头像/名字错位，看起来像「队友性别不对」。
3. **未命中 roster 时全体回落本地 actor**  
   地图 peer 已有正确 `visualGroup/visualVariant`，开战却没用。

## 修复

- `GameSession.roleSex` + `ensureSelfTeamRow` 按 `roleSex→sexGroup`
- 缓存 `2/10` peer；开战 enrich：**peer → roster → self RoleRow**
- `BattlePartyHud`：按 `selfId` 认自己；各席用自己的 `row.name/job/sex`
- `rebuildUnits`：禁止非自己席回落到 `playerActor`

## 验证

1. 一男一女组队同图：地图 peer 性别正确。
2. 遇怪开战：双方战斗体/底栏头像与地图一致（含非队长视角）。
3. 女号接受邀请后开战：自己席为女体。
