# 世界BOSS：战胜后世界播报（2026-08-29）

## 需求

玩家或队伍战胜世界 BOSS 后，向世界频道 live 播报一次。

## 落点

- `src/server/mock_server_world_boss.c` · `vm_mock_world_boss_on_battle_end(true)`
- 新函数 `vm_mock_world_boss_broadcast_victory` → `vm_mock_service_broadcast_world_chat_live`（与强化/天梯同款 live [世]）

## 文案（≤81 字节 GBK）

| 情形 | 文案 |
|------|------|
| 单人 | `恭喜玩家{名}战胜世界BOSS！` |
| 队伍（`memberCount≥2` 或战斗中 `battleMemberCount≥2`） | `恭喜{名}的队伍战胜世界BOSS！` |

主体名优先 `claimRoleId` 在线会话 `onlineRoleName`，否则当前会话/角色名。过长时回退无名字短句。

故意不用 `【世界BOSS】` / `[WBOSS:]` / `世界Boss 已刷新`，避免 Pixi `isWorldBossMapNotice` 误触发地图刷新。

## 触发

- 仅 `reward_pending` / `CLAIMED` 胜利路径；失败与普通野怪不播。
- `reward_pending` 在首个 settle 清掉，组队多成员 `note_victory` 不会重复播报。

## 复测

1. 单人 claim 并胜利 → 世界频道出现「恭喜玩家…战胜世界BOSS！」，随后仍有 despawn 刷新提示。
2. 组队（≥2）claim 并胜利 → 「恭喜…的队伍战胜世界BOSS！」。
3. 战败 / 未 claim 野怪胜利 → 无本条播报。
