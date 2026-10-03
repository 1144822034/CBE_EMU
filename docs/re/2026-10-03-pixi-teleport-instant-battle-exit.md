# 传送进野外后立刻遇怪会退出战斗

日期: 2026-10-03

## 触发

1. 传送石 / 区域免费传送 / 切图落到野外地图。
2. 落地后马上踩怪、点怪，或挂机环立刻 `4/1`。
3. 战斗 UI 刚出现就回到地图。

## 预期 / 实际

- 预期：留在战斗，可出手。
- 实际：进战画面后立刻 `showPhase("world")`。

## 契约 / 首次偏离

| 预期 | 实际（修前） |
|------|----------------|
| 同图迟到 `30/1`/`30/2` 只补 NPC/落点，不拆本场战斗 | `handleBodies` 见进图包就 `inBattle=false` |
| 传送 Promise 收尾时若已开战，保持 mmBattle | `worldMap.onTeleport` 成功一律 `showPhase("world")` |
| `onEnterScene` 只在真换图 / 死亡回城时拆战斗 | 同壳进图在 `await loadScene` 之后仍 `battle.hide()` |

传送后仍会有：

- 地图石 deferred `30/1` 之后的 poll / gateway push 再带一次同图进图
- `onEnterScene` 里 fire-and-forget 的 `refreshNearbyPeersAfterEnter`（内含 `pollOnce`）
- 目录 settle 尚未结束时挂机/碰撞已 `startBattleNearPlayer`（乐观 `inBattle=true`）

`startBattle` 把 `inBattle` 置真后，迟到进图包先清旗再 `onEnterScene`；技能/背包异步 `showPhase("battle")` 之后，进图收尾再 `showPhase("world")`。用户看到的就是进战立刻退场。

死亡普通复活 `1/7/14 result=2` 走 `20/1+30/1` **换图**（回城），`alreadyOnScene` 为假，仍会拆战斗。

## 根因

把「传送后同图补包」当成「离场进图」。真正拥有该契约的是 Pixi 进图处理，不是服务端开战包。

## 修改

| 位置 | 改动 |
|------|------|
| `GameSession.keepBattleThroughScenePacket` | 已在战斗且同图：不把 `inBattle` 清掉，不 `trackEnterScene` |
| `main.onEnterScene` | `await` 地图后若已开战且 `showsSceneFile`：不 `showPhase("world")` |
| `worldMap.onTeleport` 成功 | 已开战则不 `showPhase("world")` |

## 验证

1. 城/图传送到野外，冷却一过立刻踩怪 → 应留在战斗可出手。
2. 传送中挂机已开战 → 传送 UI 关掉后仍留在该场战斗。
3. 等数秒再踩怪 → 仍正常。
4. 野外战死选普通复活回城 → 仍应离开战斗进城镇。
5. 打完立刻再遇怪 → 仍走 `2026-09-28-pixi-encounter-instant-battle-exit.md` 的 prepend `4/8` 路径。

## 风险

- 若某条死亡复活会 `30/1` 回**同一张野外图**且战斗 UI 仍开着，同图守卫会挡住这次拆战斗。当前普通复活是回城换图。
