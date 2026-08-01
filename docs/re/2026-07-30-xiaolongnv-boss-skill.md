# 小龙女未自动放技能

日期：2026-07-30

## 触发与现象

测试打 `#88 小龙女` 时，Boss 反击始终普攻，无技能特效；日志无
`mock_battle_boss_skill`。

## 根因

Boss 主动技能闸门（`2026-07-30-boss-active-skill-counter.md`）要求：

`vm_net_mock_monster_family_for_enemy(enemyId) == VM_NET_MOCK_MONSTER_BOSS`

`#88 小龙女` 在 SCE 补目录时按名称被标成 `HUMANOID`（`2026-07-26-monster-admin-sce-catalog-gap.md`），
从未进入 Boss 技能分支。同场景 `拜月教主 #90` 已是 `BOSS`，行为不一致。

首次偏离：家族分类错误，不是技能挑选或概率逻辑故障。

## 修改

静态目录 `#88`：`HUMANOID` → `BOSS`。

若 MySQL `server_monsters` 已有 `#88` 覆盖行且 `family` 仍为非 BOSS，运行时仍以
覆盖为准；需在后台改成首领，或删除覆盖恢复静态默认。

## 验证

1. `make -j2`，重启服务端（确认无旧 family 覆盖，或后台改为首领）。
2. 打小龙女数回合：应出现 `mock_battle_boss_skill enemy=88 ...`，反击偶发技能特效。
3. `CBE_BATTLE_BOSS_SKILL=0` 仍可关闭全局 Boss 技能。
