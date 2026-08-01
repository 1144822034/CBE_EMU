# 梦境 wire→real 遇怪崩溃

## 触发

1. 管理端在 `29梦境空间_01` 配置多处战斗点，SCE 线号均为 `#200`，绑定 `real=203`。
2. 临安→使者再进梦境后踩怪发 `4/1`。
3. 客户端 `SCR_Render` 崩：`pc=0x01046C48`（`SetMapCtrlViewport`，`a2=0`→访问 `0x40`）。

## 已证伪的包形

| 包形 | 结果 |
|------|------|
| `25/12 + 2/2 + 4/5`（错位 moveinfo） | `0x01004EA8` 空视觉 |
| `25/12 + 4/10`（remap 误改 subtype-10 仍带 25/12） | 不进 mmBattle → `0x01046C48` |
| `25/12 + 4/5`（仅跳过 moveinfo） | 仍不进战 → 同 `0x01046C48`（resp=106，r5 已见 subtype=5/index） |
| **同包 lone `4/10` 作为 `4/1` 主响应**（`resp=117`，`objects=1`） | 仍不进 mmBattle；`queue_data` 后继续 moveinfo，随后 `pc=1046c48` |

梦境再进壳（`518c*`，无二次 `EnterScene`）上，subtype-5 live-node 开战不可靠；
带 `25/12` 的 subtype-10 也违反 lone `4/10` 契约。
同包 `4/10` 落在 `4/1` 主 CBMR 上会被业务回调闸门（`0x01012F8E`）吞掉，
与 `2026-07-30-instance-challenge-action13-no-battle.md` 同类。

## 根因

1. 多点共用 wire `#200` 时，`1/2/2` 与 subtype-5 live index 契约冲突。
2. 再进后的场景壳使 subtype-5 开战失败，客户端留在场景渲染并空解引用 map ctrl。
3. 可进 mmBattle 的非场景开战形是 **lone `4/10`**，且须走 **HAS_FOLLOWUP 第二段 CBMR**
   （主响应 ack / empty WT，followup pure `4/10`），不能塞进 `4/1` 同包主响应。

## 修复

仅 **梦境/FB `29*` / `b_29*`** 且 `wire != real` 时（**不改进图路径**）：

1. 解析 remap 后 arm `instanceChallengeBattlePending`（复用既有 confirm followup 管道）
2. `4/1` 主响应：有 `moveinfo` 则 empty-ack，否则 empty WT；**不**同包塞 `4/10`
3. transport 同 tick `HAS_FOLLOWUP` → `take_instance_challenge_battle_wire_followup` →
   `forceNonSceneStart` pure `4/10`（可 age≥1 再投一次）
4. 户外场景即使有绑定 remap，仍走原 `25/12+4/5`

日志：`mock_challenge_battle_remap_pure_subtype10 ... battle_delivery=data-followup`，
随后 `mock_npc_instance_challenge_battle_wire ... response=4/10`。

## 验证

1. 重启 `jh-online-server`；出口临安 → 使者再进 → 踩 remap 鬼火（**进图逻辑勿再改**）。
2. 期望：`battle_delivery=data-followup` + `instance_challenge_battle_wire`；
   客户端 `queue_data` + `queue_data_followup`，进 mmBattle。
3. 无 `0x01046C48` / `0x01004EA8`；无 remap 的存量 `#200` 仍可 `25/12+4/5`。
4. 回归：户外踩怪仍 `25/12+4/5` / mmBattle，日志**无** `remap_pure_subtype10`。

## 未决

- subtype-10 立绘仍是 `0/1` 玩家肖像回退；真立绘另证。
- 冷登录直进梦境踩 remap 同走 followup 管道；若仍崩再取证同包 vs followup。
