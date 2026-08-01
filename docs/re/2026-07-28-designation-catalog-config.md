# 称号目录对齐配置（等级 / 金币）

Date: 2026-07-28

Status: implemented (server)

```text
phase: designation catalog g_vm_net_mock_designation_entries
change: level names+thresholds; money thresholds as gold*10000 copper
keep: riches_name0..9 / level_name0..12 resource order and designation ids
```

## 契约

- 头顶资源顺序不变（客户端资源文件名）
- `role->money` 存铜币；`1 金 = 10000 铜`（与 admin `money/10000` 一致）
- 财富门槛用配置里的 `money[].min`（金币）×10000
- 等级门槛用配置里的 `level[].min`

## 验证

1. 打开称号页：已解锁行名与配置一致；头顶 gif 仍为对应 `*_nameN.gif`
2. 低金币角色看不到高档财富称号；升到对应等级后出现等级称号
3. 已装备称号若不再满足门槛，打开称号页会落到同 kind 最高已解锁项
