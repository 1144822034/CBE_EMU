# 2026-08-07 Pixi 世界频道无小喇叭提示

## 问题

世界频道（`1/3/1 type=0`）在真实 CBE 路径必须消耗 `item.dsh` **807 小喇叭**。
Pixi 聊天面板此前只发裸 `buildChatRequest`，背包无喇叭时仍可点发送；mock 对单独 `3/1` 也会回显，玩家得不到「缺道具」反馈。

## 契约

证据见 `docs/re/2026-07-20-small-horn-world-chat.md`：

```text
1/3/1  {type=0,data=<message>}
1/2/10 {Type=101}
1/7/1  {type=1,id=807,num=1}
1/2/10 {Type=101}
```

客户端应在发送前确认背包有 `807` 且 `count>=1`；缺失时提示，不发包。

## 修改

- `protocolExtras.buildWorldChatHornRequest`：按 CBE 组合包构造。
- `GameSession.sendChat(type=0)`：必要时 `openBag` 刷新；无喇叭返回
  `{ ok:false, tip:"没有小喇叭，无法发送世界消息" }`；有喇叭走组合包。
- `ChatPanel.showTip`：绿色居中 toast，风格对齐 `SystemPanel` 商城/背包提示；
  发送失败保留输入草稿。

## 验证

1. 背包无 807 → 世界频道确定 → 绿色提示，消息未发出、草稿保留。
2. 背包有 807 → 世界发送成功、服务端回显、喇叭扣减（`7/7`/`7/11`）。
3. 派/聊/伍频道不受影响。
4. 服务端落库失败 → 绿色 tip「世界消息发送失败」（见 `2026-08-07-pixi-world-chat-fail-tip.md`）。
