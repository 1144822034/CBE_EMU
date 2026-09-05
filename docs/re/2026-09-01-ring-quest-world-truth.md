# 跑环「世界真心话」（2026-09-01）

## 需求

跑环随机类型增加「世界真心话」：从 `web/fs/JHOnlineData/chatList.txt`（GBK，`序号\t句子`）抽一条，要求玩家用小喇叭在**世界频道**原文发送；服务端检测到该角色成功发出匹配消息后记进度，回跑环使提交。

## 契约

| 项 | 内容 |
|----|------|
| 类型 | `VM_MOCK_RING_TYPE_WORLD_TRUTH = 8` |
| `targetId` | chatList 加载后的 1-based 行号 |
| `targetCount` | 1 |
| 描述 | `世界真心话：世界频道发送「…」(用小喇叭，发完可提交)` |
| 完成条件 | 世界聊天投递成功且全文精确匹配（trim 空白）后 `progress=1` |
| 空池 | chatList 缺失/空时该次环回退拜访 |

钩子：`vm_net_mock_build_chat_response` 在世界频道无 system 错误后调用 `vm_mock_activities_on_world_chat(accountId, roleId, message)`。

## 验证

1. 接环出现「世界真心话」及引号内原文；追踪显示「未发送」。
2. 世界频道用小喇叭发送**完全相同**句子 → 进度「可提交」；错一字不计。
3. 本地/队伍频道发送不计。
4. 提交后环数 +1；无小喇叭无法发世界消息（既有逻辑）。
