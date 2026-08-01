# 挂机开战 subtype-5 空视觉闪退

Date: 2026-07-25

Status: root cause identified; server correction implemented

## 复现与症状

1. Android 客户端连独立 mock 服务，进入 `01桃花岛_02.sce`。
2. 点挂机按钮，客户端发 `WT 2/10`（`Type=2` + 空 `25/3`）。
3. 服务返回 `builtin-hangup-battle-start`，`resp=248`，日志含
   `index=1 pos=(102,287) enemies=2 ... source` 来自 SCE2-combat-spawn。
4. 客户端在首帧战斗绘制时闪退：

```text
地址无法访问:c type:0 size:19 value:4
pc:1004ea8 lr:5032435
r4 object (40,140) 且 +0x0C 为空指针
```

与 `2026-07-22-scene-monster-battle-start-crash.md` 同一最终症状。

## 业务链路

```text
挂机按钮
  -> JianghuOL.CBE HandleBattleEnterReq(0x01015E14) 发 2/10 Type=2
  -> builtin-hangup-battle-start
  -> 旧路径: automonster.dsh 选怪
             + select_scene_actor_moveinfo_target
               -> CBE_SERVER_ONLY 下走 SCE2-combat-spawn
             + 1/2/10 + 1/2/2 + 1/4/5 + 1/4/11
  -> mmBattle HandleBattleStartMsg(0x66CC) subtype 5
  -> 用 sceneIndex/pos 解析客户端 25 行 live scene 表
  -> 解析失败时仍拷贝越界行，视觉资源指针为空
  -> JianghuOL.CBE:0x01004EA8 LDR [unit,#0x0C] 崩溃
```

## 第一次偏离

同场景同怪的触怪开战曾成功：

```text
mock_challenge_battle_start ... index=6 pos=(102,287)
  req_index=6 req_pos=(102,287) target_source=request-live-node
```

挂机却发出：

```text
mock_hangup_battle_start ... index=1 pos=(102,287)
  source=SCE2-combat-spawn
```

坐标碰巧相同，但 **index 是 SCE 战斗出生记录序号，不是客户端 live 表下标**。
`2026-07-22` 已证明：subtype-5 必须使用客户端选中的 live-node 元组；
SCE 回退只能服务“没有 live 元组”的自治开战设想，却不能把 SCE 序号当成
live index。独立服务拿不到客户端 `R9+0x5CB0` 表时，继续发 subtype-5 就会
在首绘崩溃。

首个错误状态：服务端发出无法被客户端 live 表解析的 subtype-5
`sceneIndex/pos`（此处为 SCE `index=1`），战斗单位视觉上下文为空。

## 修复

1. 触怪 `4/1` 成功进入场景怪开战时，把
   `(scene, actorId, index, posx, posy)` 记到该连接 session。
2. 挂机开战解析目标时只允许：
   - `session-live-node`：同场景同 actor 的上次触怪元组；或
   - `emulator-live-node`：进程内模拟器 live 表扫描（禁止 SCE fallback）。
3. 两者都没有时，改为非场景开战 `1/4/10`（`target_source=non-scene-subtype10`），
   直接嵌入怪物模板，不再猜 SCE 序号。
4. 换场景 / 下线时清除 session live 缓存。

## 验证

- [x] `make -j2`
- [ ] 重启服务后，登录同图直接点挂机：日志应为
      `target_source=non-scene-subtype10 subtype=10`，客户端进战不闪退。
- [ ] 先触怪打一场，再挂机：日志应为
      `target_source=session-live-node subtype=5`，index/pos 与上次触怪一致。
- [ ] 挂机自动旗标 `4/11`、结算/逃跑仍可用。
- [ ] 普通触怪 `request-live-node` 路径未回退到 SCE。

## unresolved

- 首次挂机走 subtype-10 时怪物立绘/多怪数量是否与真实挂机完全一致，仍需对照真机包。
- 坐标扫描在错误 index 下是否总能兜底：本例与 7-22 负例表明不能依赖该兜底。
