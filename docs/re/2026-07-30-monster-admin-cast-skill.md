# 怪物管理：按怪开关反击放技能

日期：2026-07-30

## 需求

后台可配置「哪个怪反击放技能、哪个不放」，不再只能靠 `family=BOSS`。

## 契约

| 来源 | 是否放技能 |
|------|------------|
| 无 MySQL 覆盖 | `family=BOSS` → 开，其它 → 关 |
| 有 `server_monsters` 覆盖 | 读 `cast_skill`（0/1），与类型独立 |
| 环境变量 `CBE_BATTLE_BOSS_SKILL=0` | 全局关闭（仍优先生效） |

战斗闸门改为 `vm_net_mock_monster_casts_active_skill`；概率/保底/技能池逻辑不变（见 `2026-07-30-boss-active-skill-counter.md`）。

## 修改

- MySQL：`server_monsters.cast_skill`；旧库自动 `ALTER`，并把原 `family=BOSS` 行回填为 1
- 怪物管理表单：勾选「反击放技能」；目录小字显示「放技能」
- 保存/新建写入该字段；恢复默认删除覆盖后回到家族默认

## 验证

1. 重启服务端；后台打开 `#88 小龙女`，勾选放技能并保存 → 战斗日志 `mock_battle_boss_skill`
2. 取消勾选并保存 → 仅普攻反击
3. 普通怪勾选放技能亦可施放；恢复默认后回到关
