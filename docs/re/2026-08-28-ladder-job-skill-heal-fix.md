# 天梯多职业技能动画/回血修复（2026-08-28）

## 复测现象

1. 幻剑反击不放技能。
2. 鬼道放技能时播放天机技能动画（如万剑诛仙）。
3. 幻剑、鬼道 HP&lt;60% 时不自动回血。

## 根因

1. **技能池混职业**：`boss_skill_pick_id` 共用 `{1,21,121,231,201}`，含天机技；鬼道掷到 `1` 即播天机特效。
2. **回血整场一次**：`g_mockBattleMonsterHealUsedSerial` 按 battle serial 全局去重，天机回过血后幻剑/鬼道永远不能回。
3. **放技能门槛**：天梯怪若未进 MySQL 或非 BOSS/`cast_skill=0`，仅部分 ID 能放；后台天梯 `cast_skill` 未接入战斗。

## 修改

- 按槽位 `jobIndex`（天梯开战写 0/1/2）分池选进攻技：天机 `1/21/41`，幻剑 `101/121/161`，鬼道 `201/231/251`。
- 回血改为 `g_mockBattleMonsterHealUsedSerialSlots[3]`，每槽每位战斗各一次。
- 天梯进行中且配置 `cast_skill=1` 时，三职业均可放技能（不依赖怪物表 family）。

## 验证

1. `make -j2` 通过。
2. 重启服务端，开天梯：三人存活时日志应出现不同 `enemy=` / `job=` / `skill=`。
3. 各自 HP&lt;60% 后，可各出现一次 `mock_battle_boss_heal ... slot=0|1|2`。
