# 手动遇怪 vs 挂机遇怪差异（2026-08-10）

## 对照

| 环节 | 手动（碰撞 / F / 点怪） | 挂机（Pixi `2/10 Type=2`） |
|------|-------------------------|---------------------------|
| 请求 | `1/4/1` id/index/posx/posy | 原先仅 Type=2；现可带同名字段 + Operate |
| 开战包 | 几乎总是 **subtype 5**（request-live-node） | 无 live-node 时走 **subtype 10** |
| 线槽 side=1 | 玩家=1，三怪=[2,3,0] | subtype-10：玩家=0，怪=1..N |
| 战内自动 | 无 4/11；玩家点或客户端 `[自动]` 发 4/2 | 开战带 **4/11 prefer** → 服务端 poll **synth** |
| Pixi 旧行为 | 仅客户端 4/2 | synth **再加** `fireInBattleAuto` → 双通道 |

群攻 child 若在 subtype-10 上误用场景线槽 `[2,3,0]`，wire `0` 会画到玩家（漏最上怪、需再普攻等）。

## 根因与修改

1. **线槽**：`use_scene_monster_wire_maps` 禁止再用 `enemies>1 ⇒ 场景表`（心得挂机 subtype-10 三怪会误触发）。
2. **开战对齐**：Pixi 挂机带最近怪 id/index/pos + Operate → `request-live-node` subtype-5，并 `remember_last_operate`。
3. **出手单通道**：挂机只走服务端 synth；`shouldInBattleAuto` 在 `hangupActive` 时为 false（`[自动]` 仍客户端 4/2）。

## 验证

1. 户外挂机日志：`mock_hangup_battle_request_live`、`subtype=5`、`target_source=request-live-node`、`mock_hangup_seed_last_operate`。
2. 群攻三怪：特效/伤害在三只怪上，不落玩家。
3. 手动遇怪群攻仍正常；战内 `[自动]`（非挂机）仍客户端连打。
4. `make -j2`；Pixi `npx tsc --noEmit`。
