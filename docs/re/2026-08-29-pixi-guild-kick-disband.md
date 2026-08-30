# 2026-08-29 Pixi 帮派踢人 / 解散

phase: pixi-guild-kick-disband  
status: implemented

## Kick `10/40`

| 方向 | WT | 字段 |
|------|-----|------|
| 请求 | `1/10/40` | `id=目标角色`, `rid=成员位阶` |
| 应答 | `1/10/40` | `result:u8` |

结果码：`1` 成功并刷新成员页；`2` 权限不足；`3` 目标已离帮；`4` 干部须先降为帮众；其余失败。

服务端既有 `builtin-guild-kick`（仅帮主可逐出 `member_rank=3`）。

Pixi：成员列表选普通成员 →「逐出帮派」→ 确认 → `guildKick`。

## Disband `10/22 + force`

CBE 脱离是 `1/10/22 { id=roleId }`：帮主且人数>1 时失败。

Pixi「解散帮派」发：

```text
1/10/22 { id=roleId, force:u8=1 } → 10/22 { result }
```

- `force=1` 且请求者为帮主：删 `guild_applications` / `guild_members` / `guilds`
- 无 `force`：契约与 CBE 一致（独帮主离开即解散；有人时拒绝）

## UI

- 已入帮 hub：脱离帮派 / 解散帮派
- 成员页：帮主对帮众可见「逐出帮派」

## Verify

- [ ] 帮主逐出帮众 → tip「已逐出」且行消失
- [ ] 逐出干部 → tip「请先将干部降为帮众」
- [ ] 帮主解散（有成员）→ 本机无帮派
- [ ] 普通成员解散 → 失败 tip
- [ ] `npx tsx scripts/smoke-protocol.ts`；`make -j2`
