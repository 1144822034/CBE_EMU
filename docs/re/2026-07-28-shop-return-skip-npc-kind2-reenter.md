# 出店后走路二次进图（kind-2）多余

Date: 2026-07-28

Status: implemented (server)

```text
trigger: NPC map shop exit → 27/11 visible → walk
symptom: second EnterScene / loading flash on first moveinfo
root: post-catalog moveinfo armed kind-2 30/1 to rebuild portals
observation: NPCs already visible from 27/11; walk worked before 30/1
fix: NPC post-catalog → busy_ack only; skip kind-2 30/1
      empty maps still kind-2 after shell
```

## 目的（历史）vs 现状

| 包 | 目的 |
|----|------|
| poll `27/11` | 恢复 type-21 NPC 显示（mmShop 清掉后必需） |
| kind-2 `30/1` | 曾用于 `ParseMinfoAndSpawnNPCs` 重建门/碰撞节点 |

用户日志：catalog 后 NPC 已可见，走动后才 `shop_return_scene_enter 30/1` → 二次加载。该 `30/1` 不是补 NPC，是额外同场景进图。

## 契约（收窄）

```text
NPC:   shell 30/2 → poll 27/11 → post-catalog 30/2 → busy 26/0（无 kind-2）
空图:  shell 30/2 → kind-2 30/1
```

若出店后踩门失效，再单独取证恢复窄 scope 的门节点路径，勿默认二次进图。

## 验证

```text
临安/蓬莱出店：
  shop-return-poll-npc-catalog
  moveinfo → kind2_skip ... npc-catalog-live-no-reenter
  # 无 shop_return_scene_enter 30/1
  # 可走；菜单稳定
```
