# 复活后场外仍 0 血、不能遇怪

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 服务端复活已成功，客户端地图 HUD 仍 0 血，不能遇怪 | `4/7+4/8` 只更新 Battle.cbm；`pendingMapActorVitalsSync` 的 `1/1/1` 可能在 Battle 未卸完时被 scene poll 提前下发并清掉，mmGame 回来后仍持死亡 actor 缓存 | 延后投递（默认 12 tick）并连发两次；构建失败不丢 pending；武装时必要时 `scene_ready` |

## 验证

重启服务。战死买石确认后：

1. 日志先有 `map_actor_vitals_sync_arm`，约数百毫秒后两次 `map_actor_vitals_sync`。
2. 场外血蓝变为满；可正常遇怪。
