# 挂机按钮：立即开战 / 下场后停止

Date: 2026-07-28（2026-07-30 默认改为点击立即开战）

Status: implemented (server)

```text
phase: map hangup button WT 2/10 Type=2
trigger: 停止挂机卡「获取数据」；希望点挂机有系统提示节奏
```

## 根因（停止卡「获取数据」）

`HandleBattleEnterReq` 发 `2/10` 前已把战斗进入态置为 `3`。对挂机请求只回
`2/10` ACK / `4/8` 拆场、不发 `4/5|4/10`，会永久停在「获取数据」
（`2026-07-25-battle-start-rate-limit.md`、`2026-07-02-hangup-battle.md`）。

旧 `toggle-off` 软取消路径违反该契约。

## 行为

### 未挂机：第一次点挂机

1. **默认立即开战**：同包投递 hangup start（`4/5|4/10` + `4/11`），提示
   **已开始挂机**。日志 `action=immediate-start`。
2. 可选延迟：`CBE_HANGUP_START_DELAY_MS>0` 时仍走 map-side wait +
   「N秒后开始挂机」+ poll 合成开战。
3. `HangupLoopActive=1`；武装场间循环。

### 已挂机：再点挂机

1. 系统消息：**下一场完成后挂机停止**
2. 置 `HangupStopAfterBattle=1`；清 start-delay / 场间 pending / ScheduleAfterExit
3. **战中**（`OperateSessionArmed` 且未结算）：只 `2/10`+提示，打完本场
4. **地图 / 结算 / 倒计时**：fallthrough 开**最后一场**（满足 Type=2 进场契约）
5. 该场胜利后：
   - **禁止** skip-4/8（没有下一场 hangup start 清 Battle）
   - 走真实 `4/8+4/11+4/9`，清 prefer/hangup，提示 **已停止挂机**

## 日志

- `action=start-delay delay_ms=...`
- `mock_hangup_start_delay_deliver`
- `action=stop-after phase=in-fight|last-start`
- `mock_hangup_loop_schedule action=stop-after-complete` 或
  `settlement_exit` 后清 hangup

## 验证

1. 点挂机：系统「5秒后开始挂机」→ 约 5s 进战斗 →「已开始挂机」；加载框应关掉
2. 挂机中再点：系统「下一场完成后挂机停止」→ 再打一场后停，无永久「获取数据」
3. 战中关自动 `4/11 type=0` 仍立即清挂机

## 相关

- `2026-07-28-hangup-button-toggle-off.md`（旧立即软取消，已被本契约取代）
- `2026-07-25-battle-start-rate-limit.md`
- `2026-07-28-pve-settlement-blank-prompt.md`
- `2026-07-28-hangup-start-delay-dead-false.md`（battle HP=0 误清 5s 延迟）
