# 队伍 roster 绘制崩溃（调查中）

## 触发条件

两个在线角色处于同一场景时，队长发起 `1/5/1` 组队邀请，目标在原生确认框中
同意并发出 `1/5/3 { id, result=1 }`。服务端为接受方直回完整 `1/5/3`，并在
队长的下一次 event-7 场景轮询中投递：

1. `1/5/4 { id, result=1, name }`；
2. `1/5/10 { result=1, num=2, groupinfo, leadid }`。

2026-07-24 的原始运行记录在
`tmp/mock-service-live-enhancement.stdout.log:449-463`。队长收到 `resp=201` 后，
客户端在 `scene_draw_team_member_status_list(0x01014168)` 的 `0x01014388` 调用
无效头像绘制回调，LR 为 `0x0101438B`，PC 为 `0x0001FFF8`。

## 已确认的客户端契约

- `net_handle_group_info(0x01011F3A)` 对 subtype `3`、`10` 读取
  `result`、`num`、`groupinfo`；`0x01012358` 的字段名已直接读取为 `"num"`。
- 完整行是 `raw-first-id, name, sexGroup, jobIndex, online, hp, mp, hpMax, mpMax`；
  后续 id 为 tagged-u32。`AddRoleToList(0x01011E1E)` 写入 `node+34/+35` 并以
  `(jobIndex, sexGroup)` 取得头像资源。
- HUD 以 `2 * jobIndex + sexGroup - 1` 选择六个 44 字节头像资源项，并在
  `0x01014388` 调用其 `+24` 回调。
- 此次数据库角色为 `guest00023/10023 (job=1, sex=1)` 与
  `guest00024/10024 (job=3, sex=0)`，故构包归一化值分别为 `(jobIndex=0,
  sexGroup=2)` 与 `(jobIndex=2, sexGroup=1)`；两者均是有效索引。

## 根因

2026-07-24 复现的 `mock_team_groupinfo_blob` 已逐项核验；队长收到的完整二人
blob 为：

```text
00002727 0005 d0a1bada00 000102 000100 000101 ...
0004 00002728 0009 d2bbd2b6d6aac7ef00 000101 000102 000101 ...
```

它分别解码为 `10023/小黑/sexGroup=2/jobIndex=0` 与
`10024/一叶知秋/sexGroup=1/jobIndex=2`，并且四项生命值顺序正确。这排除了
`groupinfo` 行字节、blob 前缀和职业/性别归一化作为第一次偏离。

真正首先违反的是队伍生命周期：角色登录后的 `1/5/10` 请求此前获得
`{result=1,num=0,groupinfo=empty}`。但 `net_handle_group_info` 的 subtype 4
成功分支只有在 roster 计数为 `1` 时才把本机设为队长；该一行应在登录时由同一
`5/10` 建立。客户端的 subtype 3、10 都只调用 `AddRoleToList` 追加节点，完全
不清空旧列表。因此以 `5/4 + 完整 5/10` 填补空列表既不是原生增量路径，也无法
修复已有节点；错误节点最终在 HUD 被当作头像资源条目绘制，`PC=0x1FFF8` 是结果。

## 修正的协议生命周期

1. 无队伍角色的登录 `5/10` 返回仅含本机的一行完整 roster；该行不在 HUD 额外
   绘制，因为 HUD 会跳过本机 id。
2. 受邀者同意时，直回成功 `5/3` 仅带队长一行；受邀者已有本机行，因此该增量
   后恰好形成两人 roster，且 subtype 3 的第一行自然成为队长。
3. 队长的轮询先收到 `5/4` 成功结果；下一次轮询收到标准 `5/5` 新成员增量。`
   5/4` 此时可以按客户端原生分支将已有本机行标记为队长。
4. 已在队的第三方仍只收到 `5/5` 新成员增量。

这修复的是缺失的本机 roster 所有权和事件顺序，不会通过客户端回调判空、伪造
坐标或丢弃响应掩盖错误。

## 验证要求

- 冷启动/重新登录后，未组队时 roster 内仅有本机行且场景 HUD 不额外显示队员。
- 两端完成邀请、同意后均显示两人 UI；队长先收 `5/4`，随后收一个 `5/5`。
- 覆盖拒绝、普通成员离队、队长离队、重新登录和切图，确保没有重复行或遗留行。
