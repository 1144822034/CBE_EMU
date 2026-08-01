# 客户端传输：关死连接 / 不可达少重试 / 成功日志限频

日期：2026-07-31

## 修改（PC + Android 同一份 `network-client.c`）

JNI 副本：`JianghuOL/app/src/main/jni/src/cbeEmu/network-client.c`（与 `src/` 同步）。

### 1. 关死连接

- 服务端 scene-poll 成功后 `handle_client` **return 2** 结束 TCP，但 CBMR
  `flags` 仍为 0，客户端不会收到 `CLOSE_AFTER_DATA`。
- **poll：** `vm_client_remote_poll` 在 exchange 结束后始终
  `live_session_close(poll)`，避免复用已被服务端关掉的 fd（最坏卡在
  `SO_RCVTIMEO=12s`）。
- **data：** CBMR 带 `CLOSE_AFTER_DATA` 时，`live_session_exchange` 成功收包后立刻
  关 data live session；drain 侧再关一次并继续投递 CBE event 9。

### 2. 不可达时少重试

- `serviceReachable==false` 时 data 仅 **1** 次尝试（`DATA_ATTEMPTS_UNREACHABLE`），
  不再打满 5 次 × 递增 delay 占住唯一 data worker。
- poll 在不可达时 attempts=1（可达仍为 2）。
- 可达路径行为不变，短暂闪断仍可多试。

### 3. 成功日志限频

- `queue_data` / `queue_scene_poll` 成功行：默认最多约 1 条/秒，或
  `queue_ms`/`network_ms` ≥50 时强制打；带 `suppressed=`。
- `warn` / 失败 / followup 行仍全量。

## 验证

1. 进图后连续 poll：不应出现长时间卡在复用 poll socket；失败日志不应成串
   `poll_request attempt ... stage=recv reused=1` 后跟 12s 级超时。
2. 飞行模式 / 拔网：data 失败应很快返回，后续恢复靠 `reconnect_probe`。
3. 正常走动：成功 `queue_data` 日志密度明显下降。
4. 丢弃/需 CLOSE_AFTER_DATA 的路径：CBE 仍收到 event 9，且下一请求新建 TCP。
