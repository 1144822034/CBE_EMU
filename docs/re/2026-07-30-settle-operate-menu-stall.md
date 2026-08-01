# 胜利结算后操作菜单导致偶发卡死

日期：2026-07-30

## 现象

战斗胜利后结算界面偶发卡住、点不动；结算时仍弹出玩家操作菜单。

## 证据

1. `settlement_exit_arm` 后、面板延时未到，挂机又 `hangup-battle-start`
2. 曾 `action=clear-allow-reenter`：未先发 `4/8` 拆结算壳
3. `auto12_cancel reply_type=0` / `keep-prefer-poll` → `4/11 type=0` 打开操作栏
4. 挂机环 `ScheduleAfterExit` 被提前清掉后，`settlement_exit` 仍发 `4/8+4/11 type0+4/9`，在结算壳上开菜单

首次偏离：结算 UI 仍在时服务端回 `4/11 type=0`（或静默清 settle），客户端叠操作菜单。

## 根因

| 点 | 问题 |
|----|------|
| 挂机开战 | settle 未完成时曾 `prependExit=false` 静默 clear |
| `auto_clear_pending` | 胜利后清掉 `HangupStyleFlagOk`，随后 `4/12` 走 `type=0` |
| `keep-prefer-poll` | `prefer=1` 仍 ACK `type=0`，主动开菜单 |
| 挂机 `settlement_exit` | 仅认 `ScheduleAfterExit`；位清掉后仍 type0 拆场 |
| 结算中 `4/11 type=0` | 回显 type=0 叠在 `4/7` 上 |

## 修改

1. 挂机开战 settle 未清时 **prepend `4/8+4/11+4/9`**
2. `AwaitingSettlement` / exit pending 期间 `4/12` → **空 ACK**
3. **`prefer=1` 时 `4/12` 一律空 ACK**（废除 `keep-prefer-poll` type=0）；中途自动靠 `flag_poll` type=1
4. `clear_pending` 在 `prefer` 仍开时保留 FlagOk
5. 挂机/prefer 再入：结算 exit **跳过 type=0**（条件扩到 hangup loop / FlagOk），等开战 prepend 拆场
6. 结算窗内客户端 `4/11 type=0`：清 prefer/挂机，**不回显 type=0 对象**

## 验证

1. **重启**服务端（必须加载新二进制）
2. 挂机连打：结算框期间不应再出技能/道具栏
3. 日志：`hold-settle-no-menu` / `keep-auto-ui-no-type0` / `skip-type0-menu-on-settle-hangup-reenter` / `prepend-exit-reenter`
4. 不应再有 settle 窗口内的 `reply_type=0` 或 `keep-prefer-poll`
5. 结算可正常收起并进下一场
6. **手动**战斗：`settlement_exit ... response=4/8+4/11+4/9`（默认已恢复 `4/8`；见 `2026-07-30-pve-exit-empty-settle-box.md`），结算应可点、不概率卡死
