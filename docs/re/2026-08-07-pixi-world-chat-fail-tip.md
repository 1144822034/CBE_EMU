# 2026-08-07 Pixi 世界发言失败 tip 与小喇叭发包

## 现象

有小喇叭时世界频道仍出现「世界消息发送失败」（服务端 type=5 系统提示），
或仅聊天条可见、不像商城那样居中绿色 tip。

## 根因（交叉）

1. **客户端**：上一轮把世界发言打成完整 CBE 组合包
   `3/1+2/10×2+7/1`，且 `7/1` 不带背包 `seq`。`2/10 Type=101` 对 Pixi
   不是必要状态对象；喇叭行应带真实 `seq`/`itemId`。
2. **服务端**：`world_chat_store` 在 `source_name`/`message` 超过
   `VARBINARY(15)/(81)` 时直接失败并回 type=5「世界消息发送失败」。
   角色名缓冲仅 16 字节，与 `onlineRoleName[32]` 不一致时也容易踩边界。
3. **体验**：失败只进聊天条，未走与商城一致的 `showTip`。

## 修改

### Pixi

- `buildWorldChatHornRequest(text, hornSeq)`：仅 `1/3/1` + `7/1`
  （`type=1,id/itemId=807,seq,num=1`），去掉旁路 `2/10`。
- `clampWorldChatText`：GBK 长度压到 81 字节。
- `sendChat`：缺喇叭 tip「没有小喇叭…」；若回包含「世界消息发送失败」
  则 `ChatPanel.showTip` 同文案并保留草稿。

### Mock

- `vm_mock_world_chat_store`：超长 name/message **截断写入**，不再因长度
  直接失败；拒绝时打 `world_chat_store_reject` 日志。
- 聊天 builder 的 `sourceName` 扩到 32，与 session 角色名一致（落库仍截 15）。

## 验证

1. 无 807 → 绿色 tip「没有小喇叭，无法发送世界消息」。
2. 有 807 → 世界回显黄字，喇叭数量 -1。
3. MySQL 不可用时 → 绿色 tip「世界消息发送失败」，不误扣成功路径预期。

服务端改动需重新编译/重启 mock 后生效。
