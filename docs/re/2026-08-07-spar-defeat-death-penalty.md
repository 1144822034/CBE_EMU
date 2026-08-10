# 2026-08-07：切磋负方惩罚 / 离线判负

phase: spar-defeat-death-penalty-and-forfeit
status: fixed

## 契约

与打怪失败（`1/7/14` ordinary / `mock_death_penalty`）对齐：

- 当前等级进度经验 **10%**
- 携带铜钱 **5%**
- 复活 **30%** 最大 HP/MP
- 落点：最近传送石（`sMap/wMap` / SCE）

切磋：

1. **正常击杀**：败方 `battle_hp=0` → 上述惩罚；胜方仍 `+1exp/+1gold` 填非零 `4/7`
2. **任意一方离线 / 退出**（`session_mark_offline` → `duel_cancel`，且已 `startedMask≠0`）：
   离线席 `hp=0` 判负并惩罚；对端胜并收 terminal `4/7`→`4/8…`
3. 未进战（`startedMask==0`）的邀请取消：不惩罚，直接清 duel

## 修改

| 点 | 行为 |
|----|------|
| `role_apply_death_penalty_to` | 可对任意 `role*` 施罚（原 `active_role` 包装保留） |
| `session_apply_spar_death_penalty` | 切座账户 → HP=0 → 施罚 → 落点写入 session → DB save |
| `duel_arm_observer_exit` | `sparExitWasDead` 时施罚；胜方不加罚 |
| `append_duel_spar_status7` | 仅胜方 `+1exp/+1gold` |
| `build_duel_exit_packet` | 负方同包追加 `30/1` 传送石回城 |
| `duel_cancel_for_client` | 进战后断线 = forfeit |

## 验证

1. 切磋打死对方：负方经验/金钱下降、30% 血蓝、进传送石图；胜方见「挑战成功」
2. 切磋中一方断线：对端胜；断线角色库内已扣罚
3. 仅邀请未开战断线：无死亡惩罚

## Negative

- 不得对胜方施死亡惩罚
- 不得在未 `started` 的邀请取消路径扣罚
- 不得用伪造客户端闪退绕过（权威在服务端 forfeit）
