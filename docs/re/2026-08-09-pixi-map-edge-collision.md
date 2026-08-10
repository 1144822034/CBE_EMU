# PixiJS：MAP 边缘位未挡路可穿山（2026-08-09）

## 现象

部分地图「地形边」与可走区视觉重叠处，本应绕路，人物可直线穿过去。  
例：祥龙峰左下传送点左侧，可直接往上穿到应绕行的区域。

## 根因

`SceneView` 原先只用 `mapBlocksAt`：仅当 MAP 高 nibble `=== 0xF` 才挡。  
CBE `CheckMapMoveCollision*` 还会用高 nibble 的方向边缘位：

| bit | mask | 该格边 |
|-----|------|--------|
| 28 | 0x1 | 北 |
| 29 | 0x2 | 西 |
| 30 | 0x4 | 南 |
| 31 | 0x8 | 东 |

跨格时 **当前格 exit 位 OR 目标格 entry 位** 任一置位即挡（共享边）。  
边缘格常是 `1..14` 而非 `0xF`，旧逻辑当可走 → 穿山。

证据：`江湖OL.CBE` Thumb（X `0x010451C8` / Y `0x0104525C`）；文档名 VA 见 `2026-06-25-teleport-stone.md`（部分 VA 已过时）。

## 修改

| 点 | 位置 |
|----|------|
| `mapEdgeBlocksSegment` / `mapMoveBlocked` | `PixiJS_web/src/resources/map.ts` |
| 走路轴分离碰撞改 `mapMoveBlocked` | `PixiJS_web/src/render/SceneView.ts` |
| SCE 怪巡逻同契约（轴分离 + 撞墙换向；出生 snap） | `PixiJS_web/src/game/ActorLayer.ts` + `setMapCollision` |
| 仍用 `mapBlocksAt` | 落在实心内的 snap / stuck 逃生 |
| 登录卡墙（2026-08-10） | 仅 snap `0xF` 不够：落在边缘格时四向 `mapMoveBlocked` 全挡。`mapStandTrapped` + `findNearestEscapableStand`；`showScene`/`setPlayerPos` 进图纠偏；走路 stuck 同判定 |

## 验证

1. 祥龙峰左下传送点左侧：不能直线上穿，需绕路。
2. 城镇窄路（高 nibble fringe + 邻墙）仍可走，不因「非 0 全挡」卡死。
3. 野外 SCE 怪巡逻：撞墙/边缘位停步换向，不穿山；落在 `0xF` 内会 snap 到最近可走格。
4. 登录/切图若落点在墙或边缘口袋：自动弹到可走且至少有一方向可迈出的格。
5. `npx tsc --noEmit`（PixiJS_web）。
