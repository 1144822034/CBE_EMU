# 地图石二次场景加载 vs 玩家精灵

Date: 2026-07-27

Status: validated tradeoff — **保留** `27/12+posinfo`（精灵优先）

```text
phase: deferred 30/1 -> 2/3 (27/12+posinfo) -> 30/2 + poll clear -> type27/WT6
```

## 运行时对照（2026-07-27 晚）

| 方案 | 结果 |
| --- | --- |
| `27/12+posinfo` | 二次 `caller=01018150` ScreenInit；**玩家+NPC 可见** |
| `27/12-ack` + clear 后 `1/1/6`（`resp=440`×2） | NPC 可见；**玩家仍不显示**；仍可见两次 `01018150` |

结论：行走精灵契约就是 `27/12 name+posinfo` → `0x0100E9B8` →
`EnterSceneByMapName`。`1/1/6` fresh-shell **不能**替代。二次加载条是该契约的
可见代价；用同包 + poll 的无坐标 `30/2` 清 loading，不要再试验 ack/`1/1/6`
换单次加载。

## 当前实现

1. 地图石 `2/3`：`27/12+posinfo` + 27-family + `30/2-no-posinfo`
2. `map_stone_loading_clear` 延迟再清
3. **不**在 clear 后武装 `1/1/6`

## 验证

1. 地图石跨场景：日志 `response=27/12+posinfo+...`（无 `27/12-ack`、无
   `map_actor_fresh_shell_sync`）
2. 玩家与 NPC 均可见；loading 应被 clear 收掉（可接受两次进图闪一下）
