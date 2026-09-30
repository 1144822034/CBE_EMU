# 系统播报改走系统频道（2026-09-29）

## 变更

服务端 `vm_mock_service_broadcast_world_chat_live` 原先全服 fanout 为 `VM_MOCK_CHAT_TYPE_WORLD`（type 0 / `[世]`）。现改为 `VM_MOCK_CHAT_TYPE_SYSTEM`（type 5 / `[系]`）。

覆盖调用方（函数名未改，避免大范围重命名）：强化≥+8、开箱稀有、结婚、比武/连杀、天梯、世界 BOSS、猜拳、天下第一膜拜、维护公告等。

玩家小喇叭世界聊天、登录回放 `world_chat_messages` 仍为 type 0，不受影响。

## 客户端

HUD 条与聊天面板「统」页签按 type 显示；连杀称号着色/tip/音效仍按文案匹配，不依赖频道 type。
