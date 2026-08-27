# 世界地图传送石消耗恢复（2026-08-28）

## 触发与根因

提交 `3379372c8fbe3975817bf210d2092ee6511616d7` 为处理场景内传送石的背包检查，误将
世界地图传送分支 `WT 1/16/4` 的 `value` 从 `1` 改为 `0`。这两个分支不共用费用契约：

- 场景内 `n_telestone.actor` 使用独立 `16/2 {result=1,scene,posinfo,exitid}`，本来就没有
  `value`，也不消费背包传送石；
- 世界地图使用 `16/4 {curid,objid}`。`value` 是固件确认框显示的数量，也是随后传入
  `ConsumeInventoryItem(0x01018F66)` 的物品 `800` 消耗数。

因此 `value=0` 并没有只影响场景内服务，而是让世界地图的固件路径跳过背包检查与 `7/1`
道具消耗。

## 修复的协议契约

恢复世界地图确认响应：

```text
1/16/4 { result:u8=0, value:u32=1 }
```

固件现在再次显示一颗传送石的确认信息；背包没有物品 `800` 时，由它自身进入“不足／是否购买”
分支。确认且背包足够时，固件产生原有的 `16/2 + 16/3 + 7/1` 组合请求，服务端仍通过既有
`builtin-teleport-stone-confirmed-exit-combo` 处理该不透明协议流。没有变更宿主 callback、事件
类型、背包内存或场景状态。

## 验证

`teleport-stone-scene-catalog-regression` 继续验证：

1. 场景内直达 `16/2` 不包含 `value`；
2. 世界地图 `16/4` 的 `result=0,value=1`；
3. 世界地图确认目标仍仅由固件后续请求消费。

```text
make teleport-stone-scene-catalog-regression
obj\\server\\teleport-stone-scene-catalog-regression.exe
```
