# 商城买复活石后场外空血、背包残留石

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 买石复活后场外血蓝仍空，进战斗却是满的；背包多一颗 801 | `4/7+4/8` 只更新 Battle.cbm；mmGame 地图 HUD 仍持死亡 actor 缓存；过早 scene poll 可能清掉 pending；战斗终包 `7/11` 常赶不上 | 延后+双发 `1/1/1`（见 `2026-07-25-revival-map-vitals-late-sync.md`）；可选 `7/11` 清石 |

## 验证

重启服务。战死 → 买 801 → 确认：

1. 出战后场外血蓝满；日志有 `map_actor_vitals_sync`（可含 `bag_clear_seq`）。
2. 背包无多余 801；服务端 `action=revival-stone` 且 `stone_seq!=0`。
3. 再遇怪开战血蓝仍满。
