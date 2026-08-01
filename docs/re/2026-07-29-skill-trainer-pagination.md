# 技能导师列表无翻页

## 症状

学习技能 NPC 的 `26/1` 对话框只显示一页可学技能；超出
`VM_NET_MOCK_NPC_SERVICE_DIALOG_MAX_OPTIONS`（7）的条目被静默丢弃，没有「上一页 / 下一页」。

## 根因

`vm_net_mock_build_npc_service_dialog_response` 在 `OPEN_SKILLS` 分支把所有
eligible 技能一次性塞进 options；达到 7 条上限后 `continue` 跳过后续技能。
商店 / 钱庄在同一 7 槽上限下用每页 5 条 + 翻页选项，技能列表未复用该契约。

旧入口常量 `0xe4000001` 是整值相等判断，无法编码页码。

## 修改

- `OPEN_SKILLS_BASE = 0xe4000000`：`opcode=0xe4`，低 24 位为页码；NPC 根入口打开 page 0。
- 每页最多 5 条技能；需要时追加「上一页 / 下一页」（`BASE | page±1`）。
- `session->skillBrowsePage`：学习成功后仍停留在当前页（页越界则回夹）。
- 文档中历史 `0xe4000001 OPEN_SKILLS` 对应旧入口；现为 `0xe4000000` page 0。

## 验证

1. 角色等级足够、可学技能 >5：首屏 5 条 +「下一页」。
2. 点「下一页」看到后续技能；「上一页」回到 page 0。
3. 学习成功后仍显示同页剩余技能（或夹到最后一页）；货币与技能列表同步。
4. `make server -j2`。
