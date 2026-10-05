# CBE 地图结算点终止挂机仍开战

日期：2026-10-05

## 触发与现象

挂机场间已回地图，点结算「终止挂机」后仍再打一场；系统消息是「已开始挂机」，没有「已停止挂机」。

`bin/client_out.txt`：`cbe_hangup_uplink stop … compound=1`（`1/2/10,1/25/2`）之后 poll 仍下发 4/7「[挂机中]」。

## 根因

客户端已把停机包转到 19090。服务端 `mock_scene_hangup_mode` 对复合 `2/10+25/2` 过严，匹配失败则静默 return 0：

- 25/2 只认小写 `type` 的 u8，不认 `Type` / u32
- `compoundStop && !sceneHangupEnabled` 直接丢
- 场间地图壳上 `scene_is_visible` 失败也丢

未清挂机环时 poll 按 `hangup_battle_start` 再开 4/5，并提示「已开始挂机」。停机回包原先也没有 1/3/3。

## 修改

1. **服务端**：`type`/`Type`、u8/u32 都认；type=2 不再要求 scene visible / 已 enabled；清 `sceneHangupEnabled`、挂机环和 `AutoPrefer`；ACK 带「已停止挂机」。
2. **CBE 客户端**：停机后 4 秒内吞掉多余的地图 `2/10 Type=2`，避免刚清 mode 又当新开挂机。

## 验证

1. 重启服务端 + CBE。挂机打完一场回到地图结算壳，点终止挂机。
2. `server_out`：`mock_scene_hangup_mode … 25/2(type=2) response=2/10+25/2+1/3/3`、`scene_hangup_stop`、`mock_hangup_loop_clear reason=native-panel-stop`。
3. 系统消息「已停止挂机」，不再出「已开始挂机」，不再自动进下一场。
4. 数秒后再点地图挂机：仍可重新开挂。
