# 组队战斗：队友牺牲/逃跑后回合卡住

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 队友战斗中牺牲后，存活队友操作卡住 | 回合释放用 `(acted\|bit)==alive`；已出手后死亡的 bit 仍留在 `acted`，却已不在 `alive`，等式永假 | 改为 `((acted\|bit)&alive)==alive` |
| 队友逃跑后，存活队友卡住 | 逃跑走独立 `4/4`，不更新组队屏障；逃跑者仍被算作待行动成员 | `battleMemberLeftMask` 排除逃跑者；同步 shared vitals；若其余存活者已出手则 orphan flush 合并/反击并队列下发 |

## 契约

- `alive_mask`：共享 `battleMemberHp!=0` 且未在 `battleMemberLeftMask` 中。
- 回合释放：仅统计仍存活成员；死亡/逃跑不得阻塞幸存者。
- 逃跑成功：标记 left，必要时 flush 已 defer 的同伴回合。
- 逃跑失败（含反杀死亡）：写回 shared HP/敌方槽，并计入本回合已行动。

## 验证

重启 `jh-online-server.exe`，两人组队进战：

1. A 出手 defer 后 B 牺牲（或 A 出手后自己被反杀）：存活方应能继续出手并出现 `resolve=1` / `round_release` 或 `round_flush`。
2. A defer 后 B 逃跑成功：日志 `team_battle_member_exit fled=1`，随后 `team_battle_round_flush`；A 应收到合并 `4/6` 并进入怪物回合。
3. B 先逃跑、A 尚未出手：A 单独行动即可 `resolve=1`，不应再等 B。

相关日志：`team_battle_member_exit`、`team_battle_round_flush`、`team_battle_round_prepare ... alive=`。
