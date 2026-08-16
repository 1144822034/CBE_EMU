# 2026-08-13 Pixi 技能遗忘（5金）

## 需求

技能列表详情 → 操作菜单增加「遗忘(5金)」：玩家可删除已学技能，费用 5 金（50000 铜）。

## 契约（PixiJS-only）

| 方向 | 对象 | 字段 |
|------|------|------|
| 请求 | `1/12/2` | `skillid:u32` |
| 响应 | `1/12/2` | `result:u8` + `skillid:u32` |
| 成功附加 | `1/12/1` | 刷新 `learnednum` / `learnedskill` |
| 成功附加 | `1/10/26` | `money` 权威铜币余额 |

`result`：

| 值 | 含义 |
|----|------|
| 1 | 成功（已扣 5 金并从 `account_role_skills` 删除） |
| 2 | 未学会该技能 |
| 3 | 金钱不足 |
| 4 | 仅剩一个技能，禁止遗忘 |
| 5 | 存储失败 / 无角色 |

## 客户端

- `SystemPanel` 技能操作：`遗忘(5金)` → `DialogPanel` 确认（同快速修理风格）
- `GameSession.forgetSkill` → `buildSkillForgetRequest` / `parseSkillForgetResult`
- 成功后本地列表与金钱随 `12/1` + `10/26` 更新；取消/返回回到技能列表

## 服务端

- `vm_net_mock_role_service_remove_skill`：内存 + `DELETE FROM account_role_skills`
- `vm_net_mock_build_skill_forget_response`（`mock_server_catalog.c`）
- `dispatch`：`builtin-skill-forget`

费用常量：`VM_NET_MOCK_SKILL_FORGET_COST_COPPER = 5 * COPPER_PER_GOLD`。

## 验证

1. 学会 ≥2 个技能，详情 → 操作 → 遗忘(5金) → 确认：技能消失，金钱 -5金。
2. 仅 1 个技能时遗忘：提示「至少保留一个技能」。
3. 金钱不足：提示「金钱不足（需5金）」。
4. 重新登录：遗忘结果仍持久。
