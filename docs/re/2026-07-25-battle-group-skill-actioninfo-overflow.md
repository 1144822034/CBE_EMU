# 战斗群体技能卡画面（4/2 actioninfo 缓冲溢出）

```text
phase: challenge start (enemies=3) -> 4/2 group skill operate
trigger: 三怪遭遇战使用群体技能（如雷震八方/荒魂劫火）
request: WT 4/2 len=36 objects=1 first=1/4/2:27
server: unhandled wt=4/2 ... source=ignored-unhandled-server-only response=0
client: 战斗 UI 卡住（无 4/6 下行）
```

## 已确认的偏离

开战本身成功：

```text
mock_challenge_battle_start ... enemies=3 ... resp=185 process_ms=3
```

随后群体技能 `4/2` **没有任何** `mock_battle_operate` 日志，直接：

```text
unhandled wt=4/2 len=36 ... last_resp=0
ignored-unhandled-server-only ... response=0
```

首个协议偏差不是“未实现 4/2”，而是 **builder 识别了请求后在编码 `actioninfo` 时失败并返回 0**。服务端空应答使客户端一直等待战斗操作回调。

## 根因

`vm_net_mock_build_battle_operate_response` / `_fallback` 使用栈上 `actionInfo[128]`。
群体技能（`目标指向=4`）会写入：

1. 一条 type-1 多 child 动作（三怪约 85 字节）；
2. 默认 `CBE_BATTLE_BUNDLE_ROUND=1` 时，每名存活怪的反击动作（各约 49 字节）。

未全灭时合计约 **232 字节**，超过 128。`vm_net_mock_append_battle_actioninfo_record_children` /
后续 append 失败 → `return 0` → 落入 `ignored-unhandled-server-only`。

这与 `docs/re/2026-07-22-battle-group-skill-targeting.md` 的目标结算契约一致；该文已要求多 child，
但未覆盖“整回合反击捆在同一 `actioninfo`”的容量。

## 修复

- `actionInfo` 容量提升为 `VM_NET_MOCK_BATTLE_OPERATE_ACTIONINFO_CAP=512`（与队伍回合缓冲同阶）。
- 无目标 / `actioninfo` 编码失败时打印 `mock_battle_operate_abort`，避免再静默丢包。

## 验证

- [x] `make -j2`
- [ ] 三怪遭遇战使用群体技能：出现 `mock_battle_operate ... target_mode=4 targets=2|3`，有非零 `resp`
- [ ] 画面推进攻击/反击动画，不再卡在取数/等待
- [ ] 全灭与部分击杀路径均可结束或进入下一回合
