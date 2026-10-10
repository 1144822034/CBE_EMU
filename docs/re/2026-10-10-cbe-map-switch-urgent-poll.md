# CBE 切图：确认后急促 scene-poll

## 现象

地图石 / 区域传送确认后 CBE 约 2s 才进图；Pixi 很快。

## 根因

确认应答只武装服务端 deferred `30/1`，真正进图靠 scene-poll 取回。  
客户端 `VM_CLIENT_POLL_MIN_INTERVAL_MS=2000`，空闲 poll 最少隔 2s。  
服务端 `mock_teleport_stone_deferred_enter` 还拒绝确认同一 scheduler tick（~100ms）投递。

## 修复（方案 A）

[`src/network-client.c`](../../src/network-client.c) / JNI 同名文件：

1. 上行命中传送确认启发式（WT kind=16，subtype 2/3+`exitID`，或 16/4+`curid`+`objid`）→ `scene_poll_urgent_arm`
2. 武装后 150ms 起，将 poll 下限临时改为 150ms，最多约 12 次 / 2.5s
3. 收到 remote `30/1`（`sceneSubtype==1`）或窗口耗尽 → 清除急促状态
4. **全局空闲 poll 仍为 2000ms**

对照：Pixi `GameSession` 确认后 `150ms` 再 `pollOnce` 循环。

## 验证日志

```
scene_poll_urgent_arm reason=...
queue_scene_poll ...   # 约 150–400ms 内，而非 ~2s
remote_scene_target_apply ... subtype=1
```

手机需重编 `libmtksim.so` / APK 后复测。
