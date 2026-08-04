# 2026-08-04 职业进攻技改为 +攻击力 15%

## 触发

产品修正：原先对技能 raw 乘 105%（相对伤害 +5%），改为叠加当前攻击力的 15%。

## 旧 → 新

```text
旧: raw = (base + coeff) * 105 / 100
新: raw = base + coeff + attack * 15 / 100
```

之后仍：战斗丹攻击% → 抗性% → 暴击。PvE 与切磋同一公式。

## 修改点

- `src/server/mock_server_role.c`
- `src/server/mock_server_battle.c`
- 契约说明：`2026-07-31-job-skill-attack-bonus-5pct.md`

## 验证

- `make -j2`
- 抗性 0、无丹药时，技能伤害相对改前约多 `attack×0.15 - raw×0.05`
