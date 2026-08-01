# NPC 交互卡住：1/2/1 + 1/26/1 同包未拆分

## 症状

场景内点击 NPC 后客户端停住。客户端先有正常 `moveinfo` ack（`resp=11`），随后
`server request failed`；服务端出现：

```text
unhandled wt=2/1 len=58 objects=1 first=1/2/1:23,1/26/1:21
source=ignored-unhandled-server-only response=0
```

## 根因

- 客户端把待发送的移动时间线 `1/2/1`（payload≈23）与 NPC 交互 `1/26/1` 打进同一
  WT 包（与 hangup/chat 的 flush 模式相同）。
- `vm_net_mock_is_npc_dialog_request` / `is_actor_moveinfo_upload_request` 都要求
  **单对象**包，因此两者都不匹配。
- `independent_combo` 白名单含 `1/2/1`，但不含 `1/26/1`，整包校验失败 →
  `response=0`，客户端收不到 `26/1` 对话对象，界面挂起。

## 修改

在 `vm_net_mock_object_is_independent_combo_candidate` 中加入 `1/26/1`，由既有
combo 拆成：

1. `builtin-actor-moveinfo-ack`（空 `1/2/1`）
2. `builtin-npc-dialog`（`1/26/1` dialog）

## 验证

复测蓬莱等场景点 NPC：

- 服务端出现 `builtin-independent-combo` 或分拆后的 moveinfo-ack + npc-dialog
- 不再出现上述 `unhandled wt=2/1 ... 1/2/1:23,1/26/1:21`
- 客户端弹出对话，不再长时间 `server request failed`
