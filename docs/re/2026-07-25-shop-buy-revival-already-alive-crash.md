# 商城买复活石后满血仍提示「当前无需复活」并闪退

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 战死进商城买 801，血已满，仍弹「当前无需复活」，点确认闪退 | 击杀包结束时 `g_mockBattleOperateSessionArmed` 已清 0，买石误判为地图死亡并当场满血不入库；Battle.cbm 仍发 `1/7/14(result=2)`，服务端因 HP≠0 回 `20/1 result=1` 错误文案 | 战死时置 session `awaitsBattleRevivalConfirm`，买/回商城保留石头给 `1/7/14`；已满血的 `result=2` 改走 `4/7+4/8` 退出，不再发「当前无需复活」 |

## 验证

重启服务。战死 → 进商城买 801：

1. 日志应有 `battle_revival_confirm_armed`，买石 `map_revived=0` 且有背包 seq。  
2. 确认使用：`action=revival-stone` / `4/7+4/8`，无「当前无需复活」。  
3. 若仍误满血后客户端发 result=2：日志 `already-alive-battle-exit`，不闪退。
