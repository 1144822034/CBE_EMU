# 区域内传送石免费传送

## 需求

场景复活点 `n_telestone` 对话：免费传送到**当前区域**（同一 `wMap`）内的 sMap 节点。跨区域仍走世界地图 UI，消耗物品 800。

## 契约

| 路径 | 入口 | 费用 | 目的地 |
|------|------|------|--------|
| 世界/区域地图 UI | 右下角地图 → `16/4` + `16/2+16/3+7/1(800)` | 1×传送石 | 任意 wMap/sMap |
| 场景传送石 NPC | 点 `n_telestone` → 当前区域地图 UI；CBE `16/1.exitinfo` | 免费 | 仅当前 `parentWMapId` 下 sMap |

免费确认组合仍先 `16/4` 武装 provisional，再 `16/2+16/3`（无 `7/1`）。服务端 `itemObjectLen==0` 时强制 `sMap` 行与玩家当前场景同属一个 wMap，否则空 WT 拒绝。

CBE 直连 `16/1`：`exitinfo` 列为当前区域全部 sMap 行（`exitId=rowId`，标签=列2 别名 GBK）。随后单独 `16/2`/`16/3` 经 `region_teleport_stone_resolve_exit` 解析落点。

## 修改点

- `mock_server_scene_task.c`：区域 `exitinfo`、同区 resolve、免费 combo 门闩
- `WorldMapPanel.openRegionFree` / `GameSession.requestRegionFreeTeleport`
- `main.ts`：传送石点击打开区域免费地图

## 故障：部分区域免费传送进图超时（如 13剑阁_03.sce）

### 触发

站在剑阁（或终南山_02 及以后任意 sMap 叶）点 `n_telestone` → 区域地图选同区目标确认 → Pixi 报「进图超时」，服务端未武装 deferred `30/1`。

### 根因

`vm_net_mock_load_smap_region_tele_exits` 使用 `VM_NET_MOCK_REGION_TELES_MAX=48`，而 `sMap.dsh` 现有 **135** 条有效叶。截断后：

| 索引 | 行 id | 场景 |
|------|-------|------|
| 47（末条保留） | 89 | `11终南山_01.sce` |
| 58–61（被丢） | 100–103 | `13剑阁_01`…`_04.sce` |

免费路径 `region_teleport_stone_resolve_exit` / `16/1.exitinfo` 都读这份截断表：当前场景不在表内则找不到 `parentWorldId`，目标 `exitID` 也不在表内 → `reject_free_cross_region` 空 WT，客户端等到超时。付费地图 UI 走全表 `get_teleport_stone_smap_row_target`，故同目标付费传送仍可能成功。

### 修复

`VM_NET_MOCK_REGION_TELES_MAX` 提到 **192**（与 `VM_NET_MOCK_DEATH_RESPAWN_SMAP_MAX` 对齐），覆盖完整 sMap。

### 证据

- `bin/JHOnlineData/sMap.dsh`：ASCII 十进制 `rowId`/`parentWMapId`；剑阁 `parent=13`，`13剑阁_03.sce` → `exitId=102`
- Pixi `worldMap.ts` 用 `parseInt` 读全表，UI 节点正确；失败点在服务端同区门闩

## 复测

1. 站在含 `n_telestone` 的图（如蓬莱某子图）点传送石 → 区域地图，确认文案「免费瞬移到…」→ 落到同区另一图，背包 800 不变。
2. 右下角世界地图跨区传送仍扣 1 个 800。
3. CBE：点传送石应看到多条目的地列表；选同区目标免费进图。
4. **剑阁**：在 `13剑阁_01`…`_04` 任意图点传送石 → 免费传到同区另一图（含 `_03`），应见 `mock_teleport_stone_region_free_resolve` / `confirmed_exit_combo free=1`，无进图超时。
5. 终南山 / 秦始皇陵 / 泰山等原先被截断的区域同样免费可传。
