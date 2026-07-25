# 复活后场外血条仍不刷新（二次）

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 延后 `1/1/1` 仍不刷新场外血 | 1) scene poll 在 `sceneVisibleReady` 未就绪时直接 return，pending 永远发不出；2) 登录态 `1/1/1` 在 mmGame 场景轮询上常被忽略，地图买石验证过的是 `1/1/14` | 同步改为 `1/1/14`；pending 在 scene-ready 门禁之前投递；任意后续 WT（移动/遇怪等）优先投递 `1/1/14` |

## 验证

重启服务。复活确认后走动或再点遇怪：

1. 日志 `map_actor_vitals_sync ... via=wt-dispatch` 或 `via=scene-poll`。
2. 场外血蓝刷新；可遇怪。
