# 2026-08-25 好友亲密度·结婚·门派称号（Pixi）

phase: pixi-intimacy-marriage-guild-nameplate  
status: implemented

## Scope

- 好友组队野外胜场 +1 亲密度；超过 3 天未组队则打开好友列表时 -10（下限 0）
- 南宣门 `c04临安府_01` 第 4 槽种 **月老**（actorId=`20093`）；`26/1` 选项「缔结姻缘」
- 结婚条件：互为好友、`friend_degree >= 3000`、双方同在南宣门、组队、双方未婚
- 属性页 `1/1/21` 配偶名 + `spouseRoleId`；好友列表标「配偶」；`1/32/1` 传送到配偶身边
- 名板称号上方显示 `门派-{帮派名}`；对端经 `1/31/5.guild` 同步

## Wire

| 方向 | 包 | 说明 |
|------|-----|------|
| S DB | `friendships.last_team_at` / `last_decay_at` | 组队时间与衰减闩 |
| S DB | `account_roles.spouse_*` | 婚姻状态 |
| C←S | `1/1/21` | 增 `spouseRoleId` |
| C→S | `1/32/1` | 配偶传送（Pixi；CBE 可忽略） |
| S→C | scene-enter 或 `1/32/2` `{result,tip}` | 传送成功进图 / 失败 tip |
| S→C | `1/31/5` | 可选字段 `guild`（仅有帮派也可下发） |

## Client map

| 层 | 文件 |
|----|------|
| 协议 | `protocolExtras.ts`（spouseRoleId / 31/5 guild / 1/32） |
| 会话 | `GameSession`：`spouseTeleport`、好友 `isSpouse` 标记 |
| UI | `SocialPanel`：配偶行标 +「传送到身边」 |
| 名板 | `SceneView.setPlayerGuild` / `ActorLayer.refreshPeerGuild` |

## Server map

| 文件 | 作用 |
|------|------|
| `migrate_add_intimacy_marriage.sql` + `schema.sql` | 列定义 |
| `mock_server_role.c` | migration、配偶加载、亲密 bump/decay、结婚写库 |
| `mock_server_battle.c` | `team-battle*` 胜场 +1 |
| `mock_server_scene_task.c` | 南宣门月老 seed |
| `mock_server_scene_sync.c` | 月老对话 + `MARRY` 服务 |
| `mock_server_marriage.c` | `1/32` 传送 |
| `mock_server_cosmetics.c` | `31/5` guild |

## Validation

- `npx tsc --noEmit`（PixiJS_web）
- `make -j2`
- 手测：
  1. 南宣门见月老（与王朝/马汉/胡斐共 4 槽）
  2. 好友组队野外胜 → 亲密度 +1
  3. 满 3000 同图组队点「缔结姻缘」→ 属性配偶名、好友标配偶
  4. 配偶在线 → 好友「传送到身边」
  5. 有帮派角色名板称号上方 `门派-xxx`，不压 chenghao/称号高度
  6. **进图即显**：自己与附近有帮派玩家名板带 `门派-xxx`（不必先开属性/换装）

## Follow-up 2026-08-25：进图名板常缺门派

### 根因

1. **自己**：`setPlayerGuild` 仅挂在 `onCharacterStats`；进图只刷名字/称号，未拉 `1/1/21`，缓存常空。
2. **附近**：`capture_online_presence` 只刷 `onlineEq*`，从不写 `onlineGuildName`；`enqueue_peer_cosmetics` 仅在 eq 全 0 时才 DB 补齐 → 有翅/骑/称号的对端 `31/5` 经常无 `guild`。

### 修复

| 层 | 改动 |
|----|------|
| S presence | `capture_online_presence` 同步 `onlineGuildName` |
| S 31/5 | `enqueue_peer_cosmetics`：eq 或 guild 缺失都从 role DB `sync_online_cosmetics` |
| C 进图 | 立刻 `setPlayerGuild(cache)` + `openCharacterStats()` |
| C 会话 | `1/1/21` 写回 `guildName`；`31/5` 空 guild 不覆盖已知门派行 |

## Negative

- 勿把 `10/2.fdegree` 当亲密度（那是帮派/性别行）
- 勿扩 `SCENE_NPCINFO_MAX`（已用满 4 槽）
- 切磋/决斗/竞技场胜场不加亲密度（仅 `team-battle*` reason）
