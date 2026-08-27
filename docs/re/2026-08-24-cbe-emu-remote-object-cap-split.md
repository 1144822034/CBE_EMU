# CBE 模拟器 remote 模式：WT 对象数拆分

日期：2026-08-24  
范围：`src/network-client.c` / JianghuOL JNI 同名文件（`CBE_CLIENT_ONLY` remote 转发路径）

## 现象

CBE 模拟器连升级后的 live 服，游戏里弹「解包错误」。

## 契约

- `江湖OL.CBE:0x01012E4C` → `event_packet_init(packet, 10, 19)`
- `event_packet_parse_WT(0x0103467A)` 在 **响应对象数 > 10** 时返回 error 3 → UI「解包错误」
- 详见 `docs/re/2026-07-20-new-role-login-object-cap.md`、`docs/re/2026-08-04-team-invite-delay-unpack-under-load.md`

mock 服侧已按 `VM_NET_MOCK_WT_EVENT7_OBJECT_CAP=10` 做预算/拆分；remote 路径此前只做了背包 64 格 clamp 与 item-op followup 剥离，**未**对通用 compound WT 做对象数拆分。

## 修改

投递进 VM 前，`vm_client_deliver_wt_response`：

1. event-7 WT 解析对象数 ≤10：原样投递  
2. 对象数 >10：按 10 个对象一组拆成多次 event-7 回调（与 mock 服「多包投递」同类）  
3. 日志：`cbe_object_cap_split connect=… objects=N events=M`

## 未做

- **不** hook 客户端弹窗/吞掉 `event_packet_parse_WT` 错误（违反 AGENTS 根因优先）  
- **不**处理非对象数上限导致的解包失败（新字段/新 kind 需单独取证）

## 续记：版本握手超时（2026-08-24）

根因：对象数拆分时对 **所有** event-7 小包（含版本 `12/5`）在 `vm_client_deliver_wt_chunk`
里无条件 `finish_wt_packet` 重写 WT 头；解析失败或 `packet[4]==0` 时整包被丢弃 →
CBE 等不到版本响应，表现为联网超时。

修复：版本/登录等 ≤10 对象或解析失败的路径改回 **原样直投**
（`vm_client_deliver_wt_single`）；仅 `objects>10` 且完整解析成功时才拆分。
拆分失败时回退直投，不再静默丢包。

## 续记：strip/split 起算偏移错误（2026-08-24）

`client_out.txt` 有反复 `queue_scene_poll … resp=4473`，但无任何
`cbe_strip_unsupported` / `cbe_object_cap_split`。根因：strip / clamp / count /
split 用 `offset=4` 调 `vm_client_next_response_object`（要求 `offset>=5`，
`[4]=objectCount`，对象从 5 起），首轮即失败 → `objectTotal=0` 整包直送。
已改为 `offset=5`（与 `vm_client_capture_remote_observation` 一致）。

## 验证

1. `make -j2` 通过  
2. CBE 客户端连升级服，scene-sync / 登录 compound 响应 objects>10 时不再弹「解包错误」，日志见 `cbe_object_cap_split`  
3. objects≤10 的路径无额外拆分日志
